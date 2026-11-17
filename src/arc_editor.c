/* ARC 0.7.2 tile level editor: the game owns a grid of tile ids and describes its palette; the editor edits the grid in
 * place from inside the tick (from the tick's recorded input, like the UI kit), so replays and seeks redo every edit.
 * It draws a 2D top-down view over the game, or a grid over the game's own 3D view. See arc_editor_desc in arc.h. */
#include "arc_internal.h"
#include <ctype.h>

#define ED_MAX_TILES 256
#define ED_MAX_OBJS 64      /* object kinds */
#define ED_OCAP 256         /* object / setting undo records */
enum { ED_BRUSH, ED_LINE, ED_RECT, ED_FILL, ED_PICK, ED_SELECT, ED_TOOLS };
enum { ED_A_UNDO, ED_A_REDO, ED_A_SAVE, ED_A_LOAD, ED_A_PLAY, ED_A_TEST, ED_ACTIONS };
static const char *ed_tool_names[ED_TOOLS] = { "brush", "line", "rect", "fill", "pick", "select" };
static const char *ed_tool_labels[ED_TOOLS] = { "Brush B", "Line L", "Rect R", "Fill F", "Pick I", "Select M" };
static const char *ed_action_labels[ED_ACTIONS] = { "Undo", "Redo", "Save", "Load", "Play P", "Test T" };
static const arc_key ed_tool_keys[ED_TOOLS] = { ARC_KEY_B, ARC_KEY_L, ARC_KEY_R, ARC_KEY_F, ARC_KEY_I, ARC_KEY_M };
enum { ED_TAB_TILES, ED_TAB_OBJECTS, ED_TAB_LAYER, ED_TABS };
static const char *ed_tab_names[ED_TABS] = { "Tiles", "Objects", "Layer" };

typedef struct { int32_t x, y, old_id, new_id; uint32_t group; int32_t layer; } ed_rec;   /* (0.10.3 layer) */
/* everything a tick changes (saved in state snapshots so `seek` and rewinds come back to the same editor) */
typedef struct {
    uint8_t open, playing, view3d, nogrid, tool, dragging, drag_btn, panning, dirty, hover_ok, pad[2];
    int32_t sel, sx, sy, lx, ly, hx, hy;
    float zoom, panx, pany;
    uint32_t group_seq, drag_group;
    int32_t layer, orbiting;                 /* 0.10.3: the layer edited; the own 3D view's camera being dragged */
    float yaw, pitch, dist, tx, tz;          /* 0.10.3: the own 3D view's orbit camera (radians, cells) */
    /* 0.10.8 */
    int32_t tab, okind, osel, omoving;       /* the panel's tab; the object kind placed; the object selected; being dragged */
    int32_t has_sel, s0x, s0y, s1x, s1y, selecting;   /* the selection rectangle */
    int32_t pasting, onion, edit_row, edit_tab;       /* the clipboard follows the pointer; -1 / 0 / +1; a field being typed */
    char ebuf[128];
} ed_state;
/* 0.10.8 object and setting edits, undone with the cell edits of the same group. type 1 object `index` (before / after
   its "object X Y KIND ..." line; "" = not there), 2 setting `key` ("\x01" = not set), 3 the layer's name */
typedef struct { uint32_t group; int32_t layer, type, index; char key[64], before[512], after[512]; } ed_orec;

static struct {
    bool begun;
    arc_editor_desc d;
    arc_editor_tile tiles[ED_MAX_TILES];
    int16_t lut[256];                 /* id 0..255 -> tile index (-1 none) */
    int csize, stride;
    ed_rec *ring; int cap; uint64_t base, top, end;   /* undo ring: [base, top) undoable, [top, end) redoable */
    uint32_t too_big;                 /* a step that did not fit the ring (kept out of it) */
    int32_t *stack, *scratch;         /* flood fill, load diff (sized at begin) */
    ed_state s;
    char msg[160];
    /* 0.10.8 */
    arc_editor_object objs[ED_MAX_OBJS];
    void *lay[64]; char lpath[260];         /* the level's layers; its file */
    ed_orec *oring; uint64_t obase, otop, oend;
    int cw, ch; int32_t *ccells; arc_level *clip;   /* the clipboard: cells, and its objects (relative cells) in a scratch level */
} E;
static char g_ed_launch[260]; static bool g_ed_launch_done, g_ed_launch_on;

/* ---- cells ---- */
static int ed_get_raw(int x, int y) {
    uint8_t *p = (uint8_t *)E.d.cells + (size_t)y * (size_t)E.stride + (size_t)x * (size_t)E.csize;
    if (E.csize == 1) return (int)*p;
    int32_t v; memcpy(&v, p, 4); return (int)v;
}
static void ed_put_raw(int x, int y, int id) {
    uint8_t *p = (uint8_t *)E.d.cells + (size_t)y * (size_t)E.stride + (size_t)x * (size_t)E.csize;
    if (E.csize == 1) *p = (uint8_t)id; else { int32_t v = (int32_t)id; memcpy(p, &v, 4); }
}
static bool ed_in(int x, int y) { return E.begun && x >= 0 && y >= 0 && x < E.d.width && y < E.d.height; }
static int ed_tile_of(int id) {
    if (id >= 0 && id < 256) return E.lut[id];
    for (int i = 0; i < E.d.tile_count; i++) if (E.tiles[i].id == id) return i;
    return -1;
}
static bool ed_silent(void) { return arc__sim_replaying(); }
static void ed_use_layer(int l) {   /* 0.10.3 */
    if (E.d.layer_count <= 0) return;
    l = arc_clampi(l, 0, E.d.layer_count - 1);
    E.s.layer = l; E.d.cells = E.d.layers[l];
}

/* ---- undo ring ---- */
static ed_rec *ed_at(uint64_t i) { return &E.ring[i % (uint64_t)E.cap]; }
static void ed_drop_redo(void) { E.end = E.top; E.oend = E.otop; }   /* a new edit drops what could be redone */
static void ed_push(int x, int y, int old_id, int new_id, uint32_t g) {
    ed_drop_redo();
    if (g == E.too_big) return;                                /* a step bigger than the ring: not undoable at all */
    while (E.top - E.base >= (uint64_t)E.cap) {                /* full: drop the oldest whole step first */
        uint32_t og = ed_at(E.base)->group;
        if (og == g) { E.too_big = g; E.base = E.end = E.top; return; }
        while (E.base < E.top && ed_at(E.base)->group == og) E.base++;
    }
    ed_rec *r = ed_at(E.top++); r->x = x; r->y = y; r->old_id = old_id; r->new_id = new_id; r->group = g; r->layer = E.s.layer;
    E.end = E.top;
}
static bool ed_set(int x, int y, int id, uint32_t g) {
    if (!ed_in(x, y)) return false;
    int old = ed_get_raw(x, y);
    if (old == id) return false;
    ed_put_raw(x, y, id);
    ed_push(x, y, old, id, g);
    E.s.dirty = 1;
    if (E.d.changed) E.d.changed(x, y, old, id, E.d.user);
    return true;
}
static void ed_validate(void) {
    E.msg[0] = 0;
    if (E.d.validate && !E.d.validate(E.msg, sizeof E.msg, E.d.user) && !E.msg[0]) snprintf(E.msg, sizeof E.msg, "invalid");
}
static uint32_t ed_new_group(void) { return ++E.s.group_seq; }

/* ---- 0.10.8 objects, settings (in the level), their undo records ---- */
static arc_level *LV(void) { return E.begun ? E.d.level : NULL; }
static ed_orec *ed_oat(uint64_t i) { return &E.oring[i % (uint64_t)ED_OCAP]; }
static void ed_opush(int type, int index, const char *key, const char *before, const char *after, uint32_t g) {
    if (!E.oring) return;
    ed_drop_redo();
    while (E.otop - E.obase >= (uint64_t)ED_OCAP) { uint32_t og = ed_oat(E.obase)->group; while (E.obase < E.otop && ed_oat(E.obase)->group == og) E.obase++; }
    ed_orec *r = ed_oat(E.otop++);
    r->group = g; r->layer = E.s.layer; r->type = type; r->index = index;
    snprintf(r->key, sizeof r->key, "%s", key ? key : ""); snprintf(r->before, sizeof r->before, "%s", before); snprintf(r->after, sizeof r->after, "%s", after);
    E.oend = E.otop;
}
static const arc_editor_object *ed_kind(const char *kind) {
    for (int i = 0; i < E.d.object_count; i++) if (kind && !strcmp(E.objs[i].kind, kind)) return &E.objs[i];
    return NULL;
}
static int ed_kind_index(const char *kind) { const arc_editor_object *k = ed_kind(kind); return k ? (int)(k - E.objs) : -1; }
static void ed_objs_changed(void) { E.s.dirty = 1; if (E.d.objects_changed) E.d.objects_changed(E.s.layer, E.d.user); }
static int ed_nobj(void) { return LV() ? arc_level_object_count(LV(), E.s.layer) : 0; }
static void ed_oline(int i, char *out) { arc__level_object_line(LV(), E.s.layer, i, out, 512); }
/* "object X Y rest" with X Y replaced */
static void ed_line_at(const char *line, int x, int y, char *out, size_t cap) {
    int ox, oy, n = 0;
    if (sscanf(line, "object %d %d %n", &ox, &oy, &n) == 2 && n > 0) snprintf(out, cap, "object %d %d %s", x, y, line + n);
    else snprintf(out, cap, "%s", line);
}
static const char *ed_field_default(const arc_editor_field *f, char *buf, size_t n) {
    if (f->def) return f->def;
    if (f->type == ARC_FIELD_TEXT) return NULL;
    if (f->type == ARC_FIELD_CHOICE) { const char *c = f->choices ? f->choices : ""; size_t k = strcspn(c, "|"); if (k >= n) k = n - 1; memcpy(buf, c, k); buf[k] = 0; return buf; }
    if (f->type == ARC_FIELD_INT || f->type == ARC_FIELD_NUM) { snprintf(buf, n, "%g", (double)f->min); return buf; }
    return "0";
}
static int ed_obj_add(int x, int y, const char *kind, uint32_t g) {
    if (!LV() || !ed_in(x, y) || !kind || !*kind) return -1;
    int i = arc_level_object_add(LV(), E.s.layer, x, y, kind);
    if (i < 0) return -1;
    const arc_editor_object *k = ed_kind(kind);
    for (int f = 0; k && f < k->field_count; f++) {
        if (k->fields[f].optional) continue;
        char b[64]; const char *v = ed_field_default(&k->fields[f], b, sizeof b);
        if (v) arc_level_object_set(LV(), E.s.layer, i, k->fields[f].name, v);
    }
    char line[512]; ed_oline(i, line);
    if (line[0]) ed_opush(1, i, NULL, "", line, g);
    ed_objs_changed();
    return i;
}
static int ed_obj_add_line(const char *line, uint32_t g) {
    if (!LV()) return -1;
    int i = arc__level_object_insert_line(LV(), E.s.layer, -1, line);
    if (i >= 0) { char l2[512]; ed_oline(i, l2); if (l2[0]) ed_opush(1, i, NULL, "", l2, g); ed_objs_changed(); }
    return i;
}
static bool ed_obj_del(int i, uint32_t g) {
    if (!LV() || i < 0 || i >= ed_nobj()) return false;
    char line[512]; ed_oline(i, line);
    arc_level_object_remove(LV(), E.s.layer, i);
    if (line[0]) ed_opush(1, i, NULL, line, "", g);
    if (E.s.osel == i) E.s.osel = -1; else if (E.s.osel > i) E.s.osel--;
    ed_objs_changed();
    return true;
}
static bool ed_obj_set(int i, const char *key, const char *value, uint32_t g) {
    if (!LV() || i < 0 || i >= ed_nobj() || !key || !*key) return false;
    const char *cur = arc_level_object_get(LV(), E.s.layer, i, key, NULL);
    if ((!cur && !value) || (cur && value && !strcmp(cur, value))) return false;
    char before[512], after[512]; ed_oline(i, before);
    arc_level_object_set(LV(), E.s.layer, i, key, value);
    ed_oline(i, after);
    if (before[0] && after[0]) ed_opush(1, i, NULL, before, after, g);
    ed_objs_changed();
    return true;
}
static bool ed_obj_move(int i, int x, int y, uint32_t g) {
    if (!LV() || i < 0 || i >= ed_nobj() || !ed_in(x, y)) return false;
    if (arc_level_object_x(LV(), E.s.layer, i) == x && arc_level_object_y(LV(), E.s.layer, i) == y) return false;
    char before[512], after[512]; ed_oline(i, before);
    arc_level_object_move(LV(), E.s.layer, i, x, y);
    ed_oline(i, after);
    if (before[0] && after[0]) ed_opush(1, i, NULL, before, after, g);
    ed_objs_changed();
    return true;
}
static bool ed_setting_set(const char *key, const char *value, uint32_t g) {
    if (!LV() || !key || !*key) return false;
    const char *cur = arc_level_get(LV(), E.s.layer, key, NULL);
    if ((!cur && !value) || (cur && value && !strcmp(cur, value))) return false;
    char before[512]; snprintf(before, sizeof before, "%s", cur ? cur : "\x01");
    arc_level_set(LV(), E.s.layer, key, value);
    ed_opush(2, 0, key, before, value ? value : "\x01", g);
    E.s.dirty = 1;
    return true;
}
static bool ed_layer_name_set(const char *name, uint32_t g) {
    if (!LV() || !name) return false;
    const char *cur = arc_level_layer_name(LV(), E.s.layer);
    if (!strcmp(cur, name)) return false;
    char before[512]; snprintf(before, sizeof before, "%s", cur);
    arc_level_set_layer_name(LV(), E.s.layer, name);
    ed_opush(3, 0, NULL, before, name, g);
    E.s.dirty = 1;
    return true;
}
static void ed_oapply(const ed_orec *r, bool undo) {
    const char *to = undo ? r->before : r->after, *from = undo ? r->after : r->before;
    if (!LV()) return;
    if (r->type == 1) {
        if (from[0]) arc_level_object_remove(LV(), r->layer, r->index);
        if (to[0]) arc__level_object_insert_line(LV(), r->layer, r->index, to);
        E.s.osel = -1;
    } else if (r->type == 2) arc_level_set(LV(), r->layer, r->key, to[0] == 1 ? NULL : to);
    else if (r->type == 3) arc_level_set_layer_name(LV(), r->layer, to);
}

static int ed_line(int x0, int y0, int x1, int y1, int id, uint32_t g) {
    int n = 0, dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        if (ed_set(x0, y0, id, g)) n++;
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
    return n;
}
static int ed_rect(int x0, int y0, int x1, int y1, int id, bool outline, uint32_t g) {
    int ax = arc_clampi(arc_mini(x0, x1), 0, E.d.width - 1), bx = arc_clampi(arc_maxi(x0, x1), 0, E.d.width - 1);
    int ay = arc_clampi(arc_mini(y0, y1), 0, E.d.height - 1), by = arc_clampi(arc_maxi(y0, y1), 0, E.d.height - 1);
    int n = 0;
    for (int y = ay; y <= by; y++) for (int x = ax; x <= bx; x++)
        if (!outline || x == ax || x == bx || y == ay || y == by) n += ed_set(x, y, id, g) ? 1 : 0;
    return n;
}
static int ed_flood(int x, int y, int id, uint32_t g) {
    if (!ed_in(x, y)) return 0;
    int target = ed_get_raw(x, y);
    if (target == id) return 0;
    int n = 0, sp = 0, W = E.d.width, H = E.d.height;
    ed_set(x, y, id, g); n++; E.stack[sp++] = y * W + x;      /* painted when pushed: each cell is pushed once */
    while (sp > 0) {
        int c = E.stack[--sp], cx = c % W, cy = c / W;
        const int nx[4] = { cx - 1, cx + 1, cx, cx }, ny[4] = { cy, cy, cy - 1, cy + 1 };
        for (int k = 0; k < 4; k++) {
            if (nx[k] < 0 || ny[k] < 0 || nx[k] >= W || ny[k] >= H || ed_get_raw(nx[k], ny[k]) != target) continue;
            ed_set(nx[k], ny[k], id, g); n++;
            E.stack[sp++] = ny[k] * W + nx[k];
        }
    }
    return n;
}

/* ---- public API ---- */
static arc_editor_desc g_ed_orig; static bool g_ed_binding;
void arc_editor_begin(const arc_editor_desc *d0) {
    arc_editor_desc dd; memset(&dd, 0, sizeof dd); if (d0) dd = *d0;
    const arc_editor_desc *d = d0 ? &dd : NULL;
    void *lays[64]; int nlay = 0;
    if (d && d->level) {   /* 0.10.8: the level's layers are the grids */
        if (!g_ed_launch_done) {   /* --editor FILE: that file into the level, before anything is built from it */
            g_ed_launch_done = true;
            const char *v = arc_arg_value("editor");
            g_ed_launch_on = v || arc_arg_flag("editor");
            if (v) {
                snprintf(g_ed_launch, sizeof g_ed_launch, "%s", v);
                g_ed_binding = true; bool ok = arc_level_reload(d->level, v); g_ed_binding = false;
                if (!ok) { printf("ARC error editor --editor %s: %s\n", v, arc_level_error()); fflush(stdout); }
            }
        }
        arc_level *l = d->level; int n = arc_level_layer_count(l), w = arc_level_width(l, 0), h = arc_level_height(l, 0);
        for (int i = 1; i < n; i++) if (arc_level_width(l, i) != w || arc_level_height(l, i) != h) { arc_log("arc_editor_begin: the level's layers must all be the same size"); return; }
        if (n <= 0) { arc_log("arc_editor_begin: the level has no layers"); return; }
        nlay = arc_mini(n, 64);
        for (int i = 0; i < nlay; i++) lays[i] = arc_level_cells(l, i);
        dd.width = w; dd.height = h; dd.cell_type = ARC_EDITOR_CHAR; dd.stride = w + 1; dd.cells = NULL;
        dd.layers = lays; dd.layer_count = nlay; dd.layer_names = NULL;
    } else if (!g_ed_launch_done) {
        g_ed_launch_done = true; const char *v = arc_arg_value("editor");
        g_ed_launch_on = v || arc_arg_flag("editor"); if (v) snprintf(g_ed_launch, sizeof g_ed_launch, "%s", v);
    }
    bool lay = d && d->layers && d->layer_count > 0;
    if (lay) for (int i = 0; i < d->layer_count; i++) if (!d->layers[i]) lay = false;
    if (!d || (!d->cells && !lay) || d->width <= 0 || d->height <= 0 || !d->tiles || d->tile_count <= 0) { arc_log("arc_editor_begin: needs cells (or layers), a size and tiles"); return; }
    bool was_open = E.begun && E.s.open;
    free(E.ring); free(E.stack); free(E.scratch); free(E.oring); free(E.ccells); arc_level_free(E.clip);
    memset(&E, 0, sizeof E);
    if (d0) g_ed_orig = *d0;
    E.d = *d;
    if (d->level) {   /* 0.10.8 */
        memcpy(E.lay, lays, sizeof(void *) * (size_t)nlay); E.d.layers = E.lay;
        snprintf(E.lpath, sizeof E.lpath, "%s", g_ed_launch[0] ? g_ed_launch : d->level_path ? d->level_path : arc_level_path(d->level));
        E.d.level_path = E.lpath;
        E.d.object_count = arc_clampi(d->object_count, 0, ED_MAX_OBJS);
        if (d->objects && E.d.object_count) memcpy(E.objs, d->objects, sizeof(arc_editor_object) * (size_t)E.d.object_count);
        else E.d.object_count = 0;
        E.d.objects = E.objs;
        E.oring = (ed_orec *)arc__xalloc(sizeof(ed_orec) * ED_OCAP);
    } else { E.d.object_count = 0; E.d.layer_field_count = 0; }
    if (!lay) { E.d.layers = NULL; E.d.layer_count = 0; }
    else { E.d.layer_count = arc_mini(d->layer_count, 64); if (!E.d.cells) E.d.cells = d->layers[0]; for (int i = 0; i < E.d.layer_count; i++) if (d->layers[i] == E.d.cells) E.s.layer = i; }
    E.d.tile_count = arc_mini(d->tile_count, ED_MAX_TILES);
    memcpy(E.tiles, d->tiles, sizeof(arc_editor_tile) * (size_t)E.d.tile_count);
    E.d.tiles = E.tiles;
    if (E.d.erase_tile < 0 || E.d.erase_tile >= E.d.tile_count) E.d.erase_tile = 0;
    for (int i = 0; i < 256; i++) E.lut[i] = -1;
    for (int i = E.d.tile_count - 1; i >= 0; i--) if (E.tiles[i].id >= 0 && E.tiles[i].id < 256) E.lut[E.tiles[i].id] = (int16_t)i;
    E.csize = d->cell_type == ARC_EDITOR_I32 ? 4 : 1;
    E.stride = d->stride > 0 ? d->stride : d->width * E.csize;
    size_t cells = (size_t)d->width * (size_t)d->height;
    E.cap = d->undo_cells > 0 ? d->undo_cells : (int)arc_maxi(4096, (int)(cells * 2 < 0x3FFFFFFF ? cells * 2 : 0x3FFFFFFF));
    E.ring = (ed_rec *)arc__xalloc(sizeof(ed_rec) * (size_t)E.cap);
    E.stack = (int32_t *)arc__xalloc(sizeof(int32_t) * cells);
    E.scratch = (int32_t *)arc__xalloc(sizeof(int32_t) * cells);
    E.begun = true;
    E.s.zoom = 0;   /* fitted when first shown */
    E.s.dist = 0;   /* (the own 3D view: fitted when first shown) */
    E.s.osel = -1; E.s.edit_row = -1;
    if (g_ed_launch_on) E.d.any_build = true;   /* 0.10.8 --editor: F2 works on any build */
    if (was_open) { E.s.open = 1; E.s.playing = E.d.start_playing; }
    ed_validate();
    if (g_ed_launch_on && !was_open) { static bool opened; if (!opened) { opened = true; E.s.open = 1; E.s.playing = E.d.start_playing; } }
}
void arc_editor_end(void) { free(E.ring); free(E.stack); free(E.scratch); free(E.oring); free(E.ccells); arc_level_free(E.clip); memset(&E, 0, sizeof E); }
/* 0.10.8 the level was read again (hot reload, or load): the same grids, or (other sizes) begin again; no history */
void arc__editor_level_reloaded(arc_level *l) {
    if (!E.begun || E.d.level != l || g_ed_binding) return;
    int n = arc_level_layer_count(l); bool same = n > 0 && arc_mini(n, 64) == E.d.layer_count;
    for (int i = 0; same && i < E.d.layer_count; i++) same = arc_level_width(l, i) == E.d.width && arc_level_height(l, i) == E.d.height;
    if (!same) {
        ed_state keep = E.s; arc_editor_desc o = g_ed_orig; char lp[260]; snprintf(lp, sizeof lp, "%s", E.lpath); o.level_path = lp;
        arc_editor_begin(&o);
        if (E.begun) { E.s.open = keep.open; E.s.playing = keep.playing; E.s.zoom = 0; }
        return;
    }
    for (int i = 0; i < E.d.layer_count; i++) E.lay[i] = arc_level_cells(l, i);
    E.d.cells = E.lay[arc_clampi(E.s.layer, 0, E.d.layer_count - 1)];
    E.base = E.top = E.end = 0; E.obase = E.otop = E.oend = 0; E.too_big = 0;
    E.s.dirty = 0; E.s.osel = -1; E.s.dragging = 0; E.s.omoving = 0;
    ed_validate();
}
static void ed_fit(void);
void arc_editor_open(bool on) {
    if (!E.begun) return;
    if (on && !E.s.open) { E.s.playing = E.d.start_playing; E.s.dragging = 0; E.s.panning = 0; if (E.s.zoom <= 0) ed_fit(); ed_validate(); }
    E.s.open = on;
}
bool arc_editor_is_open(void) { return E.begun && E.s.open; }
void arc_editor_set_playing(bool on) { E.s.playing = on; }
bool arc_editor_playing(void) { return E.s.playing != 0; }
int  arc_editor_get(int x, int y) { return ed_in(x, y) ? ed_get_raw(x, y) : -1; }
bool arc_editor_paint(int x, int y, int id) { if (!E.begun) return false; bool r = ed_set(x, y, id, ed_new_group()); if (r) ed_validate(); return r; }
int  arc_editor_fill(int x0, int y0, int x1, int y1, int id) { if (!E.begun) return 0; int n = ed_rect(x0, y0, x1, y1, id, false, ed_new_group()); if (n) ed_validate(); return n; }
int  arc_editor_flood(int x, int y, int id) { if (!E.begun) return 0; int n = ed_flood(x, y, id, ed_new_group()); if (n) ed_validate(); return n; }
int  arc_editor_line(int x0, int y0, int x1, int y1, int id) { if (!E.begun) return 0; int n = ed_line(x0, y0, x1, y1, id, ed_new_group()); if (n) ed_validate(); return n; }
bool arc_editor_undo(void) {
    if (!E.begun) return false;
    uint32_t gc = E.top > E.base ? ed_at(E.top - 1)->group : 0, go = E.otop > E.obase ? ed_oat(E.otop - 1)->group : 0, g = gc > go ? gc : go;
    if (!g) return false;
    bool objs = false;
    while (E.otop > E.obase && ed_oat(E.otop - 1)->group == g) {   /* 0.10.8: objects first (they were made after the cells) */
        const ed_orec *r = ed_oat(--E.otop);
        if (E.d.layer_count && r->layer != E.s.layer) ed_use_layer(r->layer);
        ed_oapply(r, true); objs = objs || r->type == 1;
    }
    if (objs) ed_objs_changed();
    while (E.top > E.base && ed_at(E.top - 1)->group == g) {
        ed_rec *r = ed_at(--E.top);
        if (E.d.layer_count && r->layer != E.s.layer) ed_use_layer(r->layer);   /* 0.10.3: back on its layer */
        ed_put_raw(r->x, r->y, r->old_id);
        if (E.d.changed) E.d.changed(r->x, r->y, r->new_id, r->old_id, E.d.user);
    }
    E.s.dirty = 1; ed_validate(); return true;
}
bool arc_editor_redo(void) {
    if (!E.begun) return false;
    uint32_t gc = E.top < E.end ? ed_at(E.top)->group : 0, go = E.otop < E.oend ? ed_oat(E.otop)->group : 0;
    uint32_t g = (gc && go) ? (gc < go ? gc : go) : (gc ? gc : go);
    if (!g) return false;
    while (E.top < E.end && ed_at(E.top)->group == g) {
        ed_rec *r = ed_at(E.top++);
        if (E.d.layer_count && r->layer != E.s.layer) ed_use_layer(r->layer);
        ed_put_raw(r->x, r->y, r->new_id);
        if (E.d.changed) E.d.changed(r->x, r->y, r->old_id, r->new_id, E.d.user);
    }
    bool objs = false;
    while (E.otop < E.oend && ed_oat(E.otop)->group == g) {
        const ed_orec *r = ed_oat(E.otop++);
        if (E.d.layer_count && r->layer != E.s.layer) ed_use_layer(r->layer);
        ed_oapply(r, false); objs = objs || r->type == 1;
    }
    if (objs) ed_objs_changed();
    E.s.dirty = 1; ed_validate(); return true;
}
static int ed_steps(uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1) {   /* distinct groups in two rising runs */
    int n = 0; uint32_t last = 0; bool any = false;
    while (a0 < a1 || b0 < b1) {
        uint32_t ga = a0 < a1 ? ed_at(a0)->group : 0xFFFFFFFFu, gb = b0 < b1 ? ed_oat(b0)->group : 0xFFFFFFFFu, g = ga < gb ? ga : gb;
        if (!any || g != last) n++;
        any = true; last = g;
        if (ga == g) a0++; else b0++;
    }
    return n;
}
int arc_editor_undo_steps(void) { return E.begun ? ed_steps(E.base, E.top, E.obase, E.otop) : 0; }
static int ed_redo_steps(void) { return E.begun ? ed_steps(E.top, E.end, E.otop, E.oend) : 0; }
bool arc_editor_save(void) {
    if (!E.begun || (!E.d.save && !LV())) return false;
    E.s.dirty = 0;
    if (ed_silent()) return true;                     /* a replay or a re-simulation never writes files */
    bool ok = true;
    if (LV()) { ok = arc_level_save(LV(), E.lpath[0] ? E.lpath : NULL); if (!ok) { printf("ARC error editor save: %s\n", arc_level_error()); fflush(stdout); } }   /* 0.10.8 */
    if (ok && E.d.save) ok = E.d.save(E.d.user);
    printf(ok ? "ARC ok editor save %s\n" : "ARC error editor save %s failed\n", E.d.name ? E.d.name : ""); fflush(stdout);
    return ok;
}
bool arc_editor_load(void) {
    if (!E.begun || (!E.d.load && !LV())) return false;
    int W = E.d.width, H = E.d.height;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) E.scratch[y * W + x] = ed_get_raw(x, y);
    bool ok = true;
    if (LV()) {   /* 0.10.8: the level file again (its reload clears the history and rebinds) */
        ok = arc_level_reload(LV(), E.lpath[0] ? E.lpath : NULL);
        if (!ok) { printf("ARC error editor load: %s\n", arc_level_error()); fflush(stdout); }
        else if (!E.begun || E.d.width != W || E.d.height != H) return true;   /* (begun again at another size) */
        if (ok) ed_objs_changed();
    }
    if (ok && E.d.load) ok = E.d.load(E.d.user);
    if (ok && E.d.changed)
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { int v = ed_get_raw(x, y); if (v != E.scratch[y * W + x]) E.d.changed(x, y, E.scratch[y * W + x], v, E.d.user); }
    if (ok) { E.base = E.top = E.end = 0; E.s.dirty = 0; }
    ed_validate();
    return ok;
}
bool arc_editor_dirty(void) { return E.s.dirty != 0; }
const char *arc_editor_message(void) { return E.msg; }
bool arc_editor_hover(int *x, int *y) { if (x) *x = E.s.hx; if (y) *y = E.s.hy; return E.begun && E.s.hover_ok; }
int  arc_editor_layer(void) { return E.begun ? E.s.layer : 0; }
void arc_editor_set_layer(int layer) { if (E.begun && E.d.layer_count) { ed_use_layer(layer); E.s.dragging = 0; E.s.osel = -1; E.s.omoving = 0; E.s.edit_row = -1; ed_validate(); } }
static bool ed_has3d(void);
static bool ed_hasown(void) { return E.d.tile_mesh != NULL; }
void arc_editor_set_view(int v) {
    if (!E.begun) return;
    if (v == ARC_EDITOR_VIEW_2D || (v == ARC_EDITOR_VIEW_GAME && ed_has3d()) || (v == ARC_EDITOR_VIEW_3D && ed_hasown())) { E.s.view3d = (uint8_t)v; E.s.dragging = 0; E.s.panning = 0; E.s.orbiting = 0; }
}
int  arc_editor_view(void) { return E.begun ? E.s.view3d : 0; }

/* ---- 0.10.8 objects, settings, selection, clipboard, onion skin, test-play ---- */
arc_level *arc_editor_level(void) { return LV(); }
const char *arc_editor_launch_file(void) { return g_ed_launch; }
int  arc_editor_object_add(int x, int y, const char *kind) { int i = ed_obj_add(x, y, kind, ed_new_group()); if (i >= 0) { E.s.osel = i; ed_validate(); } return i; }
bool arc_editor_object_set(int i, const char *key, const char *value) { bool r = ed_obj_set(i, key, value, ed_new_group()); if (r) ed_validate(); return r; }
bool arc_editor_object_move(int i, int x, int y) { bool r = ed_obj_move(i, x, y, ed_new_group()); if (r) ed_validate(); return r; }
bool arc_editor_object_delete(int i) { bool r = ed_obj_del(i, ed_new_group()); if (r) ed_validate(); return r; }
int  arc_editor_object_selected(void) { return E.begun && E.s.osel < ed_nobj() ? E.s.osel : -1; }
bool arc_editor_set_setting(const char *key, const char *value) { bool r = ed_setting_set(key, value, ed_new_group()); if (r) ed_validate(); return r; }
bool arc_editor_set_layer_name(const char *name) { bool r = ed_layer_name_set(name, ed_new_group()); if (r) ed_validate(); return r; }
bool arc_editor_select(int x0, int y0, int x1, int y1) {
    if (!E.begun) return false;
    if (x0 < 0 && y0 < 0) { E.s.has_sel = 0; return true; }
    int W = E.d.width - 1, H = E.d.height - 1;
    E.s.s0x = arc_clampi(arc_mini(x0, x1), 0, W); E.s.s1x = arc_clampi(arc_maxi(x0, x1), 0, W);
    E.s.s0y = arc_clampi(arc_mini(y0, y1), 0, H); E.s.s1y = arc_clampi(arc_maxi(y0, y1), 0, H);
    E.s.has_sel = 1; return true;
}
static void ed_clip_free(void) { free(E.ccells); E.ccells = NULL; arc_level_free(E.clip); E.clip = NULL; E.cw = E.ch = 0; }
static int ed_clear_rect(int x0, int y0, int x1, int y1, uint32_t g) {
    int n = 0, erase = E.tiles[E.d.erase_tile].id;
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) n += ed_set(x, y, erase, g) ? 1 : 0;
    for (int i = ed_nobj() - 1; i >= 0; i--) {
        int ox = arc_level_object_x(LV(), E.s.layer, i), oy = arc_level_object_y(LV(), E.s.layer, i);
        if (ox >= x0 && ox <= x1 && oy >= y0 && oy <= y1) { ed_obj_del(i, g); n++; }
    }
    return n;
}
bool arc_editor_clear_selection(void) {
    if (!E.begun || !E.s.has_sel) return false;
    int n = ed_clear_rect(E.s.s0x, E.s.s0y, E.s.s1x, E.s.s1y, ed_new_group()); ed_validate(); return n > 0;
}
bool arc_editor_copy(bool cut) {
    if (!E.begun || !E.s.has_sel) return false;
    int x0 = E.s.s0x, y0 = E.s.s0y, x1 = E.s.s1x, y1 = E.s.s1y, w = x1 - x0 + 1, h = y1 - y0 + 1;
    ed_clip_free();
    E.cw = w; E.ch = h; E.ccells = (int32_t *)arc__xalloc(sizeof(int32_t) * (size_t)(w * h));
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) E.ccells[y * w + x] = ed_get_raw(x0 + x, y0 + y);
    if (LV()) {
        E.clip = arc_level_new(); arc_level_add_layer(E.clip, "clip", w, h, '.');
        for (int i = 0; i < ed_nobj(); i++) {
            int ox = arc_level_object_x(LV(), E.s.layer, i), oy = arc_level_object_y(LV(), E.s.layer, i);
            if (ox < x0 || ox > x1 || oy < y0 || oy > y1) continue;
            char line[512], l2[512]; ed_oline(i, line); if (!line[0]) continue;
            ed_line_at(line, ox - x0, oy - y0, l2, sizeof l2); arc__level_object_insert_line(E.clip, 0, -1, l2);
        }
    }
    if (cut) { ed_clear_rect(x0, y0, x1, y1, ed_new_group()); ed_validate(); }
    return true;
}
bool arc_editor_paste(int x, int y) {
    if (!E.begun || !E.ccells) return false;
    uint32_t g = ed_new_group();
    for (int cy = 0; cy < E.ch; cy++) for (int cx = 0; cx < E.cw; cx++) ed_set(x + cx, y + cy, E.ccells[cy * E.cw + cx], g);
    for (int i = 0; LV() && E.clip && i < arc_level_object_count(E.clip, 0); i++) {
        int px = x + arc_level_object_x(E.clip, 0, i), py = y + arc_level_object_y(E.clip, 0, i);
        if (!ed_in(px, py)) continue;
        char line[512], l2[512]; arc__level_object_line(E.clip, 0, i, line, sizeof line); if (!line[0]) continue;
        ed_line_at(line, px, py, l2, sizeof l2); ed_obj_add_line(l2, g);
    }
    arc_editor_select(x, y, x + E.cw - 1, y + E.ch - 1);
    ed_validate();
    return true;
}
/* FACING fields of clipboard object i turned: op 0 = 90 degrees clockwise, 1 = mirrored left-right, 2 = top-bottom */
static void ed_turn_facing(arc_level *c, int i, int op) {
    const arc_editor_object *k = ed_kind(arc_level_object_kind(c, 0, i));
    for (int f = 0; k && f < k->field_count; f++) {
        const arc_editor_field *fd = &k->fields[f];
        if (fd->type != ARC_FIELD_FACING || !arc_level_object_get(c, 0, i, fd->name, NULL)) continue;
        int n = fd->max >= 7 ? 8 : 4, v = ((int)arc_level_object_get_num(c, 0, i, fd->name, 0) % n + n) % n;
        v = op == 0 ? (v + n / 4) % n : op == 1 ? (n - v) % n : (n / 2 - v + n) % n;
        arc_level_object_set_num(c, 0, i, fd->name, (float)v);
    }
}
static void ed_clip_transform(int op) {   /* 0 rotate, 1 flip left-right, 2 flip top-bottom */
    if (!E.ccells) return;
    int w = E.cw, h = E.ch, nw = op == 0 ? h : w, nh = op == 0 ? w : h;
    int32_t *nc = (int32_t *)arc__xalloc(sizeof(int32_t) * (size_t)(w * h));
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        int nx = op == 0 ? h - 1 - y : op == 1 ? w - 1 - x : x, ny = op == 0 ? x : op == 1 ? y : h - 1 - y;
        nc[ny * nw + nx] = E.ccells[y * w + x];
    }
    free(E.ccells); E.ccells = nc; E.cw = nw; E.ch = nh;
    if (E.clip) {
        arc_level *n = arc_level_new(); arc_level_add_layer(n, "clip", nw, nh, '.');
        for (int i = 0; i < arc_level_object_count(E.clip, 0); i++) {
            int x = arc_level_object_x(E.clip, 0, i), y = arc_level_object_y(E.clip, 0, i);
            int nx = op == 0 ? h - 1 - y : op == 1 ? w - 1 - x : x, ny = op == 0 ? x : op == 1 ? y : h - 1 - y;
            char line[512], l2[512]; arc__level_object_line(E.clip, 0, i, line, sizeof line); if (!line[0]) continue;
            ed_line_at(line, nx, ny, l2, sizeof l2);
            int j = arc__level_object_insert_line(n, 0, -1, l2); if (j >= 0) ed_turn_facing(n, j, op);
        }
        arc_level_free(E.clip); E.clip = n;
    }
}
void arc_editor_rotate_clipboard(void) { if (E.begun) ed_clip_transform(0); }
void arc_editor_flip_clipboard(bool vertical) { if (E.begun) ed_clip_transform(vertical ? 2 : 1); }
void arc_editor_onion(int which) { if (E.begun) E.s.onion = which < 0 ? -1 : which > 0 ? 1 : 0; }
static int ed_onion_layer(void) {
    if (!E.s.onion || E.d.layer_count < 2) return -1;
    int l = E.s.layer + E.s.onion; return (l >= 0 && l < E.d.layer_count) ? l : -1;
}
static int ed_get_layer(int l, int x, int y) {
    uint8_t *p = (uint8_t *)E.d.layers[l] + (size_t)y * (size_t)E.stride + (size_t)x * (size_t)E.csize;
    if (E.csize == 1) return (int)*p;
    int32_t v; memcpy(&v, p, 4); return (int)v;
}
bool arc_editor_playtest(int x, int y) {
    if (!E.begun || !E.d.playtest) return false;
    if (LV() && E.lpath[0] && !ed_silent()) {   /* a copy of the level as it is now, beside it */
        char p[300]; size_t n = 0; snprintf(p, sizeof p, "%s.playtest", E.lpath);
        char *t = arc_level_text(LV(), &n); arc_file_write(p, t, n); free(t);
    }
    E.s.open = 0; E.s.dragging = 0; E.s.pasting = 0; E.s.edit_row = -1; E.s.omoving = 0; E.s.selecting = 0;
    printf("ARC ok editor playtest layer %d at %d,%d\n", E.s.layer, x, y); fflush(stdout);
    E.d.playtest(E.s.layer, x, y, E.d.user);
    return true;
}

/* ---- layout (the same in tick and draw) ---- */
typedef struct { float x, y, w, h; int kind, index, part; } ed_box;   /* kind 0 tool, 1 action, 2 tile, 3 object kind, 4 field (index = row; part 0 -, 1 value, 2 +), 5 tab, 6 delete object */
static ed_box ed_boxes[ED_TOOLS + ED_ACTIONS + ED_MAX_TILES + ED_MAX_OBJS + ED_TABS + 3 * 64 + 4]; static int ed_nboxes;
static float ed_label_y[64]; static int ed_nlabels;   /* each field row's label line (drawn left of its boxes) */
/* 0.10.8 the panel's tabs and field rows */
static bool ed_tab_ok(int t) { return t == ED_TAB_TILES || (t == ED_TAB_OBJECTS && LV() && E.d.object_count > 0) || (t == ED_TAB_LAYER && LV() != NULL); }
static const arc_editor_field ED_NAME_FIELD = { "name", ARC_FIELD_TEXT, 0, 0, 0, NULL, NULL, false };
static const arc_editor_object *ed_osel_kind(void) { int i = E.s.osel; return (i >= 0 && i < ed_nobj()) ? ed_kind(arc_level_object_kind(LV(), E.s.layer, i)) : NULL; }
static int ed_rows(int tab) {
    if (tab == ED_TAB_OBJECTS) { const arc_editor_object *k = ed_osel_kind(); return k ? arc_mini(k->field_count, 63) : 0; }
    if (tab == ED_TAB_LAYER) return LV() ? 1 + arc_mini(E.d.layer_field_count, 62) : 0;
    return 0;
}
static const arc_editor_field *ed_row_field(int tab, int row) {
    if (tab == ED_TAB_OBJECTS) { const arc_editor_object *k = ed_osel_kind(); return (k && row >= 0 && row < k->field_count) ? &k->fields[row] : NULL; }
    if (tab == ED_TAB_LAYER) { if (row == 0) return &ED_NAME_FIELD; return (E.d.layer_fields && row - 1 < E.d.layer_field_count) ? &E.d.layer_fields[row - 1] : NULL; }
    return NULL;
}
static const char *ed_row_get(int tab, int row) {
    const arc_editor_field *f = ed_row_field(tab, row); if (!f || !LV()) return NULL;
    if (tab == ED_TAB_OBJECTS) return arc_level_object_get(LV(), E.s.layer, E.s.osel, f->name, NULL);
    if (row == 0) return arc_level_layer_name(LV(), E.s.layer);
    return arc_level_get(LV(), E.s.layer, f->name, NULL);
}
static bool ed_row_set(int tab, int row, const char *v, uint32_t g) {
    const arc_editor_field *f = ed_row_field(tab, row); if (!f) return false;
    bool r = tab == ED_TAB_OBJECTS ? ed_obj_set(E.s.osel, f->name, v, g) : row == 0 ? ed_layer_name_set(v ? v : "", g) : ed_setting_set(f->name, v, g);
    if (r) ed_validate();
    return r;
}
static int ed_choice_count(const char *c) { int n = 1; for (; c && *c; c++) if (*c == '|') n++; return c ? n : 0; }
static void ed_choice(const char *c, int k, char *out, size_t n) {
    out[0] = 0; if (!c) return;
    for (int i = 0; i < k && c; i++) { c = strchr(c, '|'); if (c) c++; }
    if (!c) return;
    size_t m = strcspn(c, "|"); if (m >= n) m = n - 1; memcpy(out, c, m); out[m] = 0;
}
static const char *ED_FACE4[4] = { "N", "E", "S", "W" }, *ED_FACE8[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
/* the shown value of a row ("-" for an unset optional field) */
static void ed_row_text(int tab, int row, char *out, size_t n) {
    const arc_editor_field *f = ed_row_field(tab, row); const char *v = ed_row_get(tab, row);
    if (!f) { out[0] = 0; return; }
    if (!v) { snprintf(out, n, "-"); return; }
    if (f->type == ARC_FIELD_FACING) { int k = f->max >= 7 ? 8 : 4, d = ((atoi(v) % k) + k) % k; snprintf(out, n, "%s", k == 8 ? ED_FACE8[d] : ED_FACE4[d]); }
    else if (f->type == ARC_FIELD_BOOL) snprintf(out, n, "%s", atoi(v) ? "yes" : "no");
    else snprintf(out, n, "%s", v);
}
/* -1 / +1 on a row (wrapping for FACING and CHOICE); shift: 10 steps */
static void ed_row_step(int tab, int row, int dir, bool shift, uint32_t g) {
    const arc_editor_field *f = ed_row_field(tab, row); if (!f) return;
    const char *v = ed_row_get(tab, row); char b[128];
    if (f->type == ARC_FIELD_INT || f->type == ARC_FIELD_NUM) {
        float st = (f->step > 0 ? f->step : 1.0f) * (shift ? 10.0f : 1.0f), x = v ? (float)atof(v) : (f->def ? (float)atof(f->def) : f->min) - st * (float)dir;
        x += st * (float)dir;
        if (f->max > f->min) x = arc_clampf(x, f->min, f->max);
        if (f->type == ARC_FIELD_INT) snprintf(b, sizeof b, "%d", (int)floorf(x + 0.5f)); else snprintf(b, sizeof b, "%g", (double)x);
    } else if (f->type == ARC_FIELD_FACING) {
        int k = f->max >= 7 ? 8 : 4, d = v ? atoi(v) : 0; d = (((d + dir) % k) + k) % k; snprintf(b, sizeof b, "%d", d);
    } else if (f->type == ARC_FIELD_CHOICE) {
        int n = ed_choice_count(f->choices), cur = -1; char c[64];
        for (int i = 0; v && i < n; i++) { ed_choice(f->choices, i, c, sizeof c); if (!strcmp(c, v)) cur = i; }
        cur = n ? (((cur < 0 ? (dir > 0 ? -1 : 0) : cur) + dir) % n + n) % n : 0; ed_choice(f->choices, cur, b, sizeof b);
    } else if (f->type == ARC_FIELD_BOOL) snprintf(b, sizeof b, "%d", v && atoi(v) ? 0 : 1);
    else return;
    ed_row_set(tab, row, b, g);
}
typedef struct { float k, ts, pw, sh, rh, ax, ay, aw, ah, px; } ed_geom;
static ed_geom ed_layout(void) {
    ed_geom g; float W = (float)arc_width(), H = (float)arc_height();
    g.k = arc_dpi_scale(); g.ts = arc_maxf(1.0f, floorf(1.5f * g.k * 2.0f) / 2.0f);
    if (W < 640) g.ts = 1;
    g.pw = arc_minf(230.0f * g.k * g.ts / 1.5f, floorf(W * 0.35f));
    g.sh = 8.0f * g.ts + 8.0f * g.k; g.rh = 8.0f * g.ts + 6.0f * g.k;
    g.ax = 0; g.ay = 0; g.aw = W - g.pw; g.ah = H - g.sh; g.px = W - g.pw;
    float pad = 4.0f * g.k, y = pad + g.rh + pad, bw = (g.pw - 3 * pad) * 0.5f;
    ed_nboxes = 0;
    for (int i = 0; i < ED_TOOLS + ED_ACTIONS; i++) {
        ed_box *b = &ed_boxes[ed_nboxes++];
        int col = i % 2, row = i / 2;
        b->x = g.px + pad + (float)col * (bw + pad); b->y = y + (float)row * (g.rh + 2 * g.k); b->w = bw; b->h = g.rh;
        b->kind = i < ED_TOOLS ? 0 : 1; b->index = i < ED_TOOLS ? i : i - ED_TOOLS; b->part = 0;
    }
    y += (float)((ED_TOOLS + ED_ACTIONS + 1) / 2) * (g.rh + 2 * g.k) + pad * 2;
    ed_nlabels = 0;
    if (ed_tab_ok(ED_TAB_OBJECTS) || ed_tab_ok(ED_TAB_LAYER)) {   /* 0.10.8 tabs */
        int nt = 0; for (int t = 0; t < ED_TABS; t++) nt += ed_tab_ok(t) ? 1 : 0;
        float tw = (g.pw - pad * (float)(nt + 1)) / (float)nt; int k = 0;
        for (int t = 0; t < ED_TABS; t++) {
            if (!ed_tab_ok(t)) continue;
            ed_box *b = &ed_boxes[ed_nboxes++]; b->x = g.px + pad + (float)k * (tw + pad); b->y = y; b->w = tw; b->h = g.rh; b->kind = 5; b->index = t; b->part = 0; k++;
        }
        y += g.rh + pad * 2;
    }
    if (E.s.tab != ED_TAB_TILES && ed_tab_ok(E.s.tab)) {
        if (E.s.tab == ED_TAB_OBJECTS) {   /* the kinds, two to a row, then the selected object's fields */
            float bw2 = (g.pw - 3 * pad) * 0.5f;
            for (int i = 0; i < E.d.object_count; i++) {
                ed_box *b = &ed_boxes[ed_nboxes++]; b->x = g.px + pad + (float)(i % 2) * (bw2 + pad); b->y = y + (float)(i / 2) * g.rh; b->w = bw2; b->h = g.rh - g.k; b->kind = 3; b->index = i; b->part = 0;
            }
            y += (float)((E.d.object_count + 1) / 2) * g.rh + pad * 2;
        }
        int rows = ed_rows(E.s.tab); float lw = (g.pw - 2 * pad) * 0.42f, vx = g.px + pad + lw, vw = g.pw - 2 * pad - lw;
        for (int r = 0; r < rows; r++) {
            const arc_editor_field *f = ed_row_field(E.s.tab, r); if (!f) continue;
            ed_label_y[ed_nlabels++] = y;
            bool steps = f->type == ARC_FIELD_INT || f->type == ARC_FIELD_NUM || f->type == ARC_FIELD_FACING || f->type == ARC_FIELD_CHOICE;
            float sw = steps ? g.rh : 0;
            for (int part = 0; part < 3; part++) {
                if (!steps && part != 1) continue;
                ed_box *b = &ed_boxes[ed_nboxes++]; b->kind = 4; b->index = r; b->part = part; b->y = y; b->h = g.rh - g.k;
                b->x = part == 0 ? vx : part == 1 ? vx + sw + (steps ? g.k : 0) : vx + vw - sw; b->w = part == 1 ? vw - 2 * sw - (steps ? 2 * g.k : 0) : sw;
            }
            y += g.rh;
        }
        if (E.s.tab == ED_TAB_OBJECTS && ed_osel_kind()) {
            ed_box *b = &ed_boxes[ed_nboxes++]; b->x = g.px + pad; b->y = y + pad; b->w = g.pw - 2 * pad; b->h = g.rh; b->kind = 6; b->index = 0; b->part = 0;
        }
        return g;
    }
    int n = E.d.tile_count; float avail = g.ah - y - pad; int per_col = arc_maxi(1, (int)(avail / g.rh));
    int cols = arc_clampi((n + per_col - 1) / per_col, 1, 4); float cw = (g.pw - pad * (float)(cols + 1)) / (float)cols;
    for (int i = 0; i < n; i++) {
        ed_box *b = &ed_boxes[ed_nboxes++];
        int col = i / per_col, row = i % per_col;
        if (col >= cols) { ed_nboxes--; break; }
        b->x = g.px + pad + (float)col * (cw + pad); b->y = y + (float)row * g.rh; b->w = cw; b->h = g.rh - g.k; b->kind = 2; b->index = i; b->part = 0;
    }
    return g;
}
static void ed_fit(void) {
    ed_geom g = ed_layout();
    float z = E.d.cell_px > 0 ? E.d.cell_px : floorf(arc_minf((g.aw - 16) / (float)E.d.width, (g.ah - 16) / (float)E.d.height));
    E.s.zoom = arc_clampf(z, 2, 128);
    E.s.panx = floorf(g.ax + (g.aw - E.s.zoom * (float)E.d.width) * 0.5f);
    E.s.pany = floorf(g.ay + (g.ah - E.s.zoom * (float)E.d.height) * 0.5f);
}
static bool ed_has3d(void) { return E.d.world_cell > 0 && E.d.camera; }
/* 0.10.3 the own 3D view: cell size, its orbit camera over the editing area */
static float ed_cell(void) { return E.d.world_cell > 0 ? E.d.world_cell : 1.0f; }
static void ed_fit3d(void) {
    float c = ed_cell();
    E.s.tx = E.d.origin.x + (float)E.d.width * c * 0.5f; E.s.tz = E.d.origin.z + (float)E.d.height * c * 0.5f;
    E.s.yaw = 0; E.s.pitch = 0.95f; E.s.dist = (float)arc_maxi(E.d.width, E.d.height) * c * 0.85f;
}
static arc_camera ed_cam3d(void) {
    arc_camera cam; memset(&cam, 0, sizeof cam);
    float cp = cosf(E.s.pitch), c = ed_cell();
    cam.target = arc_v3_make(E.s.tx, E.d.origin.y, E.s.tz);
    cam.eye = arc_v3_add(cam.target, arc_v3_make(E.s.dist * cp * sinf(E.s.yaw), E.s.dist * sinf(E.s.pitch), E.s.dist * cp * cosf(E.s.yaw)));
    cam.up = arc_v3_make(0, 1, 0); cam.fov_y = 0.9f; cam.znear = arc_maxf(0.05f, E.s.dist * 0.01f); cam.zfar = E.s.dist * 4.0f + (float)arc_maxi(E.d.width, E.d.height) * c * 2.0f;
    return cam;
}
/* the cell under the pointer: 2D view, or a ray from the game's camera against the ground plane */
static bool ed_pick(arc_v2 m, int *cx, int *cy) {
    bool own = E.s.view3d == ARC_EDITOR_VIEW_3D && ed_hasown();
    if ((E.s.view3d == ARC_EDITOR_VIEW_GAME && ed_has3d()) || own) {
        arc_camera cam; memset(&cam, 0, sizeof cam);
        arc_v3 o, dir;
        if (own) { ed_geom g = ed_layout(); cam = ed_cam3d(); arc_camera_ray(&cam, g.aw, g.ah, arc_v2_make(m.x - g.ax, m.y - g.ay), &o, &dir); }
        else { E.d.camera(&cam, E.d.user); arc_camera_ray(&cam, (float)arc_width(), (float)arc_height(), m, &o, &dir); }
        if (fabsf(dir.y) < 1e-6f) return false;
        float t = (E.d.origin.y - o.y) / dir.y; if (t <= 0) return false;
        float wx = (o.x + dir.x * t - E.d.origin.x) / ed_cell(), wz = (o.z + dir.z * t - E.d.origin.z) / ed_cell();
        *cx = (int)floorf(wx); *cy = (int)floorf(wz);
    } else {
        if (E.s.zoom <= 0) return false;
        *cx = (int)floorf((m.x - E.s.panx) / E.s.zoom); *cy = (int)floorf((m.y - E.s.pany) / E.s.zoom);
    }
    return ed_in(*cx, *cy);
}

/* ---- the tick: returns true when the game's tick should not run ---- */
static bool ed_key_free(arc_key k, bool shift) {   /* a letter the editor uses itself, unless a tile takes it (then SHIFT + it) */
    if (!arc_key_pressed(k)) return false;
    for (int i = 0; i < E.d.tile_count; i++) if (E.tiles[i].key == k) return shift;
    return true;
}
static void ed_edit_commit(void) {   /* 0.10.8: a field being typed */
    if (E.s.edit_row < 0) return;
    int row = E.s.edit_row, tab = E.s.edit_tab; E.s.edit_row = -1;
    const arc_editor_field *f = ed_row_field(tab, row); if (!f) return;
    const char *v = E.s.ebuf; char b[64];
    if (f->type == ARC_FIELD_INT || f->type == ARC_FIELD_NUM) {
        if (!*v && f->optional) v = NULL;
        else { float x = (float)atof(v); if (f->max > f->min) x = arc_clampf(x, f->min, f->max); if (f->type == ARC_FIELD_INT) snprintf(b, sizeof b, "%d", (int)floorf(x + 0.5f)); else snprintf(b, sizeof b, "%g", (double)x); v = b; }
    } else if (!*v && f->optional && tab == ED_TAB_OBJECTS) v = NULL;
    ed_row_set(tab, row, v, ed_new_group());
}
static void ed_edit_begin(int tab, int row) {
    const char *v = ed_row_get(tab, row);
    E.s.edit_row = row; E.s.edit_tab = tab; snprintf(E.s.ebuf, sizeof E.s.ebuf, "%s", v ? v : "");
}
bool arc__editor_tick(void) {
    if (!E.begun) return false;
    bool ctrl = arc_key_down(ARC_KEY_CTRL), shift = arc_key_down(ARC_KEY_SHIFT), alt = arc_key_down(ARC_KEY_ALT);
    if (arc_key_pressed(ARC_KEY_F2) && (E.d.any_build || arc__dev_keys())) { arc_editor_open(!E.s.open); return true; }
    if (!E.s.open) return false;
    if (!ed_tab_ok(E.s.tab)) E.s.tab = ED_TAB_TILES;
    if (E.s.osel >= ed_nobj()) E.s.osel = -1;
    bool typing = E.s.edit_row >= 0;
    if (typing) {   /* 0.10.8: typing into a field: letters go to it, Enter keeps it, ESC drops it */
        const char *t = arc_text_input(); size_t n = strlen(E.s.ebuf);
        for (; *t; t++) if ((unsigned char)*t >= 32 && (unsigned char)*t < 127 && n + 1 < sizeof E.s.ebuf) { E.s.ebuf[n++] = *t; E.s.ebuf[n] = 0; }
        if (arc_key_pressed(ARC_KEY_BACKSPACE) && n > 0) E.s.ebuf[--n] = 0;
        if (arc_key_pressed(ARC_KEY_ENTER)) { ed_edit_commit(); typing = false; }
        else if (arc_key_pressed(ARC_KEY_ESCAPE)) { E.s.edit_row = -1; return !E.s.playing; }
    }
    if (!typing && arc_key_pressed(ARC_KEY_ESCAPE) && !arc_ui_wants_keyboard()) {
        if (E.s.pasting) E.s.pasting = 0;
        else if (E.s.selecting || E.s.has_sel) { E.s.selecting = 0; E.s.has_sel = 0; }
        else { arc_editor_open(false); return true; }
    }
    if (E.s.zoom <= 0) ed_fit();
    ed_geom g = ed_layout();
    /* keys */
    if (typing) {
    } else if (ctrl) {
        if (arc_key_pressed(ARC_KEY_Z)) { if (shift) arc_editor_redo(); else arc_editor_undo(); }
        if (arc_key_pressed(ARC_KEY_Y)) arc_editor_redo();
        if (arc_key_pressed(ARC_KEY_S)) arc_editor_save();
        if (arc_key_pressed(ARC_KEY_O)) arc_editor_load();
        if (arc_key_pressed(ARC_KEY_C)) arc_editor_copy(false);   /* 0.10.8 */
        if (arc_key_pressed(ARC_KEY_X)) arc_editor_copy(true);
        if (arc_key_pressed(ARC_KEY_V) && E.ccells) { E.s.pasting = 1; E.s.dragging = 0; }
    } else if (E.s.pasting) {   /* 0.10.8: R turns the clipboard, F mirrors it (SHIFT: top-bottom) */
        if (arc_key_pressed(ARC_KEY_R)) arc_editor_rotate_clipboard();
        if (arc_key_pressed(ARC_KEY_F)) arc_editor_flip_clipboard(shift);
    } else {
        bool tile_key = false;
        if (E.s.tab == ED_TAB_OBJECTS)   /* 0.10.8: in the Objects tab an object kind's key wins */
            for (int i = 0; i < E.d.object_count; i++) if (E.objs[i].key && arc_key_pressed(E.objs[i].key)) { E.s.okind = i; tile_key = true; break; }
        if (!tile_key) for (int i = 0; i < E.d.tile_count; i++) if (E.tiles[i].key && arc_key_pressed(E.tiles[i].key) && !(shift && (E.tiles[i].key == ARC_KEY_B || E.tiles[i].key == ARC_KEY_L || E.tiles[i].key == ARC_KEY_R || E.tiles[i].key == ARC_KEY_F || E.tiles[i].key == ARC_KEY_I || E.tiles[i].key == ARC_KEY_M))) { E.s.sel = i; tile_key = true; break; }
        if (!tile_key) {
            for (int t = 0; t < ED_TOOLS; t++) {
                if (!arc_key_pressed(ed_tool_keys[t])) continue;
                bool taken = false;
                for (int i = 0; i < E.d.tile_count; i++) if (E.tiles[i].key == ed_tool_keys[t]) taken = true;
                if (!taken || shift) { E.s.tool = (uint8_t)t; E.s.dragging = 0; }
            }
            if (ed_key_free(ARC_KEY_G, shift)) E.s.nogrid = !E.s.nogrid;
            if (ed_key_free(ARC_KEY_P, shift)) E.s.playing = !E.s.playing;
            if (ed_key_free(ARC_KEY_V, shift) && (ed_has3d() || ed_hasown())) {   /* 2D -> own 3D -> game view (0.10.3) */
                int v = E.s.view3d;
                for (int k = 0; k < 3; k++) {
                    v = v == ARC_EDITOR_VIEW_2D ? ARC_EDITOR_VIEW_3D : v == ARC_EDITOR_VIEW_3D ? ARC_EDITOR_VIEW_GAME : ARC_EDITOR_VIEW_2D;
                    if (v == ARC_EDITOR_VIEW_2D || (v == ARC_EDITOR_VIEW_GAME && ed_has3d()) || (v == ARC_EDITOR_VIEW_3D && ed_hasown())) break;
                }
                arc_editor_set_view(v);
            }
            if (E.d.layer_count > 1 && ed_key_free(ARC_KEY_O, shift)) E.s.onion = E.s.onion == 0 ? -1 : E.s.onion < 0 ? 1 : 0;   /* 0.10.8 onion: above, below, off */
            if (E.d.playtest && ed_key_free(ARC_KEY_T, shift)) {   /* 0.10.8 test-play from the cell under the pointer (else the middle) */
                int tx = E.s.hover_ok ? E.s.hx : E.d.width / 2, ty = E.s.hover_ok ? E.s.hy : E.d.height / 2;
                arc_editor_playtest(tx, ty); return true;
            }
        }
        if (arc_key_pressed(ARC_KEY_TAB)) { int t = E.s.tab; for (int k = 0; k < ED_TABS; k++) { t = (t + 1) % ED_TABS; if (ed_tab_ok(t)) break; } E.s.tab = t; }
        if (arc_key_pressed(ARC_KEY_DELETE)) { if (E.s.has_sel) arc_editor_clear_selection(); else if (E.s.osel >= 0) arc_editor_object_delete(E.s.osel); }
        if (E.d.layer_count > 1 && arc_key_pressed(ARC_KEY_PAGEUP)) { arc_editor_set_layer(E.s.layer - 1); E.s.osel = -1; }   /* 0.10.3 layers */
        if (E.d.layer_count > 1 && arc_key_pressed(ARC_KEY_PAGEDOWN)) { arc_editor_set_layer(E.s.layer + 1); E.s.osel = -1; }
        if (arc_key_pressed(ARC_KEY_LBRACKET)) E.s.sel = (E.s.sel + E.d.tile_count - 1) % E.d.tile_count;
        if (arc_key_pressed(ARC_KEY_RBRACKET)) E.s.sel = (E.s.sel + 1) % E.d.tile_count;
        bool own = E.s.view3d == ARC_EDITOR_VIEW_3D && ed_hasown();
        if (arc_key_pressed(ARC_KEY_HOME)) { if (own) ed_fit3d(); else ed_fit(); }
        float step = 8.0f * g.k;
        if (own) {   /* 0.10.3: arrows move the orbit centre along the view */
            float sp = E.s.dist * 0.02f, fx = -sinf(E.s.yaw), fz = -cosf(E.s.yaw);
            float mx = (arc_key_down(ARC_KEY_RIGHT) ? 1.0f : 0.0f) - (arc_key_down(ARC_KEY_LEFT) ? 1.0f : 0.0f), mz = (arc_key_down(ARC_KEY_UP) ? 1.0f : 0.0f) - (arc_key_down(ARC_KEY_DOWN) ? 1.0f : 0.0f);
            E.s.tx += (fx * mz - fz * mx) * sp; E.s.tz += (fz * mz + fx * mx) * sp;
        } else {
            if (arc_key_down(ARC_KEY_LEFT)) E.s.panx += step;
            if (arc_key_down(ARC_KEY_RIGHT)) E.s.panx -= step;
            if (arc_key_down(ARC_KEY_UP)) E.s.pany += step;
            if (arc_key_down(ARC_KEY_DOWN)) E.s.pany -= step;
        }
    }
    if (E.s.sel < 0 || E.s.sel >= E.d.tile_count) E.s.sel = 0;
    if (E.s.okind < 0 || E.s.okind >= E.d.object_count) E.s.okind = 0;
    g = ed_layout();   /* (the tab or the selected object may have changed the panel) */
    /* mouse */
    arc_v2 m = arc_mouse();
    bool over_panel = m.x >= g.px, over_status = m.y >= g.ah && !over_panel;
    bool lp = arc_key_pressed(ARC_MOUSE_LEFT), rp = arc_key_pressed(ARC_MOUSE_RIGHT);
    if ((lp || rp) && E.s.edit_row >= 0) {   /* a click anywhere but the field keeps what was typed */
        bool on_it = false;
        for (int i = 0; i < ed_nboxes; i++) { const ed_box *b = &ed_boxes[i]; if (b->kind == 4 && b->index == E.s.edit_row && b->part == 1 && m.x >= b->x && m.y >= b->y && m.x < b->x + b->w && m.y < b->y + b->h) on_it = true; }
        if (!on_it) { ed_edit_commit(); g = ed_layout(); }
    }
    if (over_panel && (lp || rp)) {
        for (int i = 0; i < ed_nboxes; i++) {
            const ed_box *b = &ed_boxes[i];
            if (m.x < b->x || m.y < b->y || m.x >= b->x + b->w || m.y >= b->y + b->h) continue;
            if (b->kind == 4) {   /* 0.10.8 a field: - / + step (right-click on the value: back one, or clear an optional field) */
                const arc_editor_field *f = ed_row_field(E.s.tab, b->index);
                if (!f) break;
                if (b->part != 1) ed_row_step(E.s.tab, b->index, b->part == 0 ? -1 : 1, shift, ed_new_group());
                else if (rp && f->optional) ed_row_set(E.s.tab, b->index, NULL, ed_new_group());
                else if (rp) ed_row_step(E.s.tab, b->index, -1, shift, ed_new_group());
                else if (f->type == ARC_FIELD_TEXT || f->type == ARC_FIELD_INT || f->type == ARC_FIELD_NUM) ed_edit_begin(E.s.tab, b->index);
                else ed_row_step(E.s.tab, b->index, 1, shift, ed_new_group());
                break;
            }
            if (!lp) break;
            if (b->kind == 0) { E.s.tool = (uint8_t)b->index; E.s.dragging = 0; }
            else if (b->kind == 2) E.s.sel = b->index;
            else if (b->kind == 3) E.s.okind = b->index;
            else if (b->kind == 5) E.s.tab = b->index;
            else if (b->kind == 6) { if (E.s.osel >= 0) arc_editor_object_delete(E.s.osel); }
            else if (b->index == ED_A_UNDO) arc_editor_undo();
            else if (b->index == ED_A_REDO) arc_editor_redo();
            else if (b->index == ED_A_SAVE) arc_editor_save();
            else if (b->index == ED_A_LOAD) arc_editor_load();
            else if (b->index == ED_A_PLAY) E.s.playing = !E.s.playing;
            else if (b->index == ED_A_TEST && E.d.playtest) { arc_editor_playtest(E.s.hover_ok ? E.s.hx : E.d.width / 2, E.s.hover_ok ? E.s.hy : E.d.height / 2); return true; }
            break;
        }
    }
    /* 0.10.3 the own 3D view: orbit (middle drag, or SPACE + right drag), pan (SHIFT + middle, or SPACE + left), zoom (wheel) */
    if (E.s.view3d == ARC_EDITOR_VIEW_3D && ed_hasown()) {
        if (E.s.dist <= 0) ed_fit3d();
        bool space = arc_key_down(ARC_KEY_SPACE);
        if (!over_panel && (arc_key_pressed(ARC_MOUSE_MIDDLE) || (space && (lp || rp)))) E.s.orbiting = (shift && arc_key_pressed(ARC_MOUSE_MIDDLE)) || (space && lp) ? 2 : 1;
        if (E.s.orbiting) {
            arc_v2 d = arc_mouse_delta();
            if (E.s.orbiting == 1) { E.s.yaw -= d.x * 0.008f; E.s.pitch = arc_clampf(E.s.pitch + d.y * 0.008f, 0.15f, 1.55f); }
            else {
                float sp = E.s.dist * 0.0025f, cy = cosf(E.s.yaw), sy = sinf(E.s.yaw);
                E.s.tx += (-cy * d.x - sy * d.y) * sp; E.s.tz += (sy * d.x - cy * d.y) * sp;
            }
            if (!arc_key_down(ARC_MOUSE_MIDDLE) && !arc_key_down(ARC_MOUSE_LEFT) && !arc_key_down(ARC_MOUSE_RIGHT)) E.s.orbiting = 0;
        }
        float wh = arc_mouse_wheel();
        if (wh != 0 && !over_panel) E.s.dist = arc_clampf(E.s.dist * powf(0.85f, wh), ed_cell() * 2.0f, (float)arc_maxi(E.d.width, E.d.height) * ed_cell() * 4.0f);
        if (E.s.orbiting) { E.s.hover_ok = 0; return !E.s.playing; }
    }
    /* pan (middle drag, or SPACE + left drag) and zoom (wheel) in the 2D view */
    bool view2d = E.s.view3d == ARC_EDITOR_VIEW_2D || (E.s.view3d == ARC_EDITOR_VIEW_GAME && !ed_has3d()) || (E.s.view3d == ARC_EDITOR_VIEW_3D && !ed_hasown());
    if (view2d) {
        if (!over_panel && (arc_key_pressed(ARC_MOUSE_MIDDLE) || (lp && arc_key_down(ARC_KEY_SPACE)))) E.s.panning = 1;
        if (E.s.panning) {
            arc_v2 d = arc_mouse_delta(); E.s.panx += d.x; E.s.pany += d.y;
            if (!arc_key_down(ARC_MOUSE_MIDDLE) && !arc_key_down(ARC_MOUSE_LEFT)) E.s.panning = 0;
        }
        float wh = arc_mouse_wheel();
        if (wh != 0 && !over_panel) {
            float nz = arc_clampf(E.s.zoom * powf(1.25f, wh), 2, 128);
            nz = floorf(nz + 0.5f); if (nz == E.s.zoom) nz = arc_clampf(E.s.zoom + (wh > 0 ? 1.0f : -1.0f), 2, 128);
            E.s.panx = floorf(m.x - (m.x - E.s.panx) * nz / E.s.zoom); E.s.pany = floorf(m.y - (m.y - E.s.pany) * nz / E.s.zoom);
            E.s.zoom = nz;
        }
    }
    int cx = -1, cy = -1;
    E.s.hover_ok = (!over_panel && !over_status && ed_pick(m, &cx, &cy)) ? 1 : 0;
    E.s.hx = cx; E.s.hy = cy;
    if (E.s.panning) return !E.s.playing;
    bool on_grid = E.s.hover_ok && !over_panel;
    /* 0.10.8 pasting: left places the clipboard at the pointer, right gives up */
    if (E.s.pasting) {
        if (lp && on_grid) { arc_editor_paste(cx, cy); E.s.pasting = 0; }
        else if (rp) E.s.pasting = 0;
        return !E.s.playing;
    }
    /* 0.10.8 the select tool: drag a rectangle (right-click drops it) */
    if (E.s.tool == ED_SELECT && !E.s.omoving) {
        if (lp && on_grid) { E.s.selecting = 1; E.s.has_sel = 1; E.s.s0x = E.s.s1x = E.s.sx = cx; E.s.s0y = E.s.s1y = E.s.sy = cy; }
        else if (rp) { E.s.has_sel = 0; E.s.selecting = 0; }
        if (E.s.selecting) {
            if (E.s.hover_ok) arc_editor_select(E.s.sx, E.s.sy, cx, cy);
            if (!arc_key_down(ARC_MOUSE_LEFT)) E.s.selecting = 0;
        }
        return !E.s.playing;
    }
    /* 0.10.8 the Objects tab: left places one (or picks and drags the one there), right removes it */
    if (E.s.tab == ED_TAB_OBJECTS && LV() && !E.s.dragging) {
        if (lp && on_grid) {
            int i = arc_level_object_at(LV(), E.s.layer, cx, cy, 0);
            if (i >= 0) { E.s.osel = i; E.s.omoving = 1; E.s.sx = E.s.lx = cx; E.s.sy = E.s.ly = cy; }
            else if (E.d.object_count > 0) arc_editor_object_add(cx, cy, E.objs[E.s.okind].kind);
        } else if (rp && on_grid) {
            int i = arc_level_object_at(LV(), E.s.layer, cx, cy, 0);
            if (i >= 0) arc_editor_object_delete(i);
        }
        if (E.s.omoving) {
            if (E.s.hover_ok) { E.s.lx = cx; E.s.ly = cy; }
            if (!arc_key_down(ARC_MOUSE_LEFT)) { E.s.omoving = 0; if (E.s.lx != E.s.sx || E.s.ly != E.s.sy) arc_editor_object_move(E.s.osel, E.s.lx, E.s.ly); }
        }
        return !E.s.playing;
    }
    /* tools */
    int sel_id = E.tiles[E.s.sel].id, erase_id = E.tiles[E.d.erase_tile].id;
    if (!E.s.dragging && (lp || rp) && on_grid) {
        int id = lp ? sel_id : erase_id;
        int tool = alt ? ED_PICK : E.s.tool;
        if (tool == ED_PICK) { int t = ed_tile_of(ed_get_raw(cx, cy)); if (t >= 0) E.s.sel = t; }
        else if (tool == ED_FILL) { if (ed_flood(cx, cy, id, ed_new_group())) ed_validate(); }
        else {
            E.s.dragging = 1; E.s.drag_btn = lp ? 1 : 2; E.s.sx = E.s.lx = cx; E.s.sy = E.s.ly = cy; E.s.drag_group = ed_new_group();
            if (tool == ED_BRUSH) ed_set(cx, cy, id, E.s.drag_group);
        }
    } else if (E.s.dragging) {
        bool held = arc_key_down(E.s.drag_btn == 1 ? ARC_MOUSE_LEFT : ARC_MOUSE_RIGHT);
        int id = E.s.drag_btn == 1 ? sel_id : erase_id;
        if (E.s.hover_ok) { E.s.lx = cx; E.s.ly = cy; }
        if (E.s.tool == ED_BRUSH && held && E.s.hover_ok && (cx != E.s.sx || cy != E.s.sy)) {   /* sx, sy: the last painted cell */
            ed_line(E.s.sx, E.s.sy, cx, cy, id, E.s.drag_group); E.s.sx = cx; E.s.sy = cy;
        }
        if (!held) {
            if (E.s.tool == ED_LINE) ed_line(E.s.sx, E.s.sy, E.s.lx, E.s.ly, id, E.s.drag_group);
            else if (E.s.tool == ED_RECT) ed_rect(E.s.sx, E.s.sy, E.s.lx, E.s.ly, id, shift, E.s.drag_group);
            E.s.dragging = 0;
            ed_validate();
        }
    }
    return !E.s.playing;
}

/* ---- drawing (render only: reads the state the ticks left) ---- */
static uint32_t ed_text_col(uint32_t rgb) {
    int r = (int)((rgb >> 16) & 255), gg = (int)((rgb >> 8) & 255), b = (int)(rgb & 255);
    return r * 3 + gg * 6 + b > 1000 ? 0x101010 : 0xF0F0F0;
}
static void ed_draw_cell(int id, float x, float y, float z, float inset) {
    int t = ed_tile_of(id);
    uint32_t col = t >= 0 ? (E.tiles[t].rgb & 0xFFFFFF) : 0xFF00FF;
    arc_draw_rect(x + inset, y + inset, z - inset, z - inset, col);
    if (t >= 0 && E.tiles[t].icon.id) {
        const arc_editor_tile *tl = &E.tiles[t]; int tw = 0, th = 0; arc_texture_size(tl->icon, &tw, &th);
        if (tw > 0 && th > 0) {
            float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
            if (tl->sw > 0 && tl->sh > 0) { u0 = tl->sx / (float)tw; v0 = tl->sy / (float)th; u1 = (tl->sx + tl->sw) / (float)tw; v1 = (tl->sy + tl->sh) / (float)th; }
            arc_draw_sprite(tl->icon, x + inset, y + inset, z - inset, z - inset, u0, v0, u1, v1, 0xFFFFFF);
        }
    } else if (z >= 12 && E.csize == 1 && id > 32 && id < 127 && id != E.tiles[E.d.erase_tile].id) {   /* (the erase tile, usually floor, stays plain) */
        char s[2] = { (char)id, 0 }; float sc = floorf((z - 2) / 10.0f); if (sc < 1) sc = 1;
        arc_draw_text(x + (z - 8 * sc) * 0.5f + inset * 0.5f, y + (z - 8 * sc) * 0.5f + inset * 0.5f, sc, ed_text_col(col), s);
    }
}
/* 0.10.8 an object's marker in the 2D view: its colour (or icon) inset in the cell, its first letter, a facing tick */
static int ed_obj_facing(arc_level *l, int layer, int i, int *n) {
    const arc_editor_object *k = ed_kind(arc_level_object_kind(l, layer, i));
    for (int f = 0; k && f < k->field_count; f++) if (k->fields[f].type == ARC_FIELD_FACING) {
        const char *v = arc_level_object_get(l, layer, i, k->fields[f].name, NULL); if (!v) return -1;
        *n = k->fields[f].max >= 7 ? 8 : 4; return ((atoi(v) % *n) + *n) % *n;
    }
    return -1;
}
static void ed_draw_object(arc_level *l, int layer, int i, float x, float y, float z, float alpha, bool selected) {
    const char *kind = arc_level_object_kind(l, layer, i); const arc_editor_object *k = ed_kind(kind);
    uint32_t rgb = k ? (k->rgb & 0xFFFFFF) : 0xFF40FF; float in = arc_maxf(1.0f, floorf(z * 0.18f));
    if (selected) arc_draw_rect(x, y, z, z, arc_rgba(0xFFFFFF, 0.35f * alpha));
    if (k && k->icon.id) {
        int tw = 0, th = 0; arc_texture_size(k->icon, &tw, &th); float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
        if (k->sw > 0 && k->sh > 0 && tw > 0 && th > 0) { u0 = k->sx / (float)tw; v0 = k->sy / (float)th; u1 = (k->sx + k->sw) / (float)tw; v1 = (k->sy + k->sh) / (float)th; }
        arc_draw_sprite(k->icon, x + in, y + in, z - 2 * in, z - 2 * in, u0, v0, u1, v1, arc_rgba(0xFFFFFF, alpha));
    } else {
        arc_draw_rect(x + in, y + in, z - 2 * in, z - 2 * in, arc_rgba(rgb, alpha));
        arc_draw_rect_lines(x + in, y + in, z - 2 * in, z - 2 * in, arc_rgba(0x000000, 0.6f * alpha));
        const char *nm = k && k->name ? k->name : kind;
        if (z >= 12 && nm && nm[0]) { char s[2] = { (char)toupper((unsigned char)nm[0]), 0 }; float sc = floorf((z - 2 * in) / 10.0f); if (sc < 1) sc = 1; arc_draw_text(x + (z - 8 * sc) * 0.5f, y + (z - 8 * sc) * 0.5f, sc, arc_rgba(ed_text_col(rgb), alpha), s); }
    }
    int n = 4, f = ed_obj_facing(l, layer, i, &n);
    if (f >= 0 && z >= 8) {   /* a tick from the middle toward where it faces */
        float a = (float)f * (6.2831853f / (float)n), cx = x + z * 0.5f, cy = y + z * 0.5f;
        arc_draw_line2d(cx, cy, cx + sinf(a) * z * 0.48f, cy - cosf(a) * z * 0.48f, arc_maxf(1.0f, z * 0.08f), arc_rgba(0xFFFFFF, alpha));
    }
    if (selected) arc_draw_rect_lines(x, y, z + 1, z + 1, arc_rgba(0xFFE060, alpha));
}
static void ed_cell_outline(int cx, int cy, uint32_t rgb) {
    arc_draw_rect_lines(E.s.panx + (float)cx * E.s.zoom, E.s.pany + (float)cy * E.s.zoom, E.s.zoom + 1, E.s.zoom + 1, rgb);
}
static void ed_preview_line(int x0, int y0, int x1, int y1, uint32_t rgb, bool in3d) {
    int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy, guard = 0;
    for (;;) {
        if (in3d) { float c = E.d.world_cell; arc_v3 o = E.d.origin; arc_draw_box_lines(arc_v3_make(o.x + (float)x0 * c, o.y, o.z + (float)y0 * c), arc_v3_make(o.x + (float)(x0 + 1) * c, o.y + c * 0.1f, o.z + (float)(y0 + 1) * c), rgb); }
        else ed_cell_outline(x0, y0, rgb);
        if ((x0 == x1 && y0 == y1) || ++guard > 100000) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
/* 0.10.3 the own 3D view: a viewport over the editing area, the game's meshes for every cell, the grid on the floor */
static void ed_box3d(int x0, int y0, int x1, int y1, float h, uint32_t rgb) {
    float c = ed_cell(); arc_v3 o = E.d.origin;
    arc_draw_box_lines(arc_v3_make(o.x + (float)x0 * c, o.y, o.z + (float)y0 * c), arc_v3_make(o.x + (float)(x1 + 1) * c, o.y + c * h, o.z + (float)(y1 + 1) * c), rgb);
}
static void ed_render3d(ed_geom g) {
    if (E.s.dist <= 0) ed_fit3d();
    arc_viewport_begin((int)g.ax, (int)g.ay, (int)g.aw, (int)g.ah);
    arc__view_effects_off(true); arc_set_draw_cell(0);   /* the whole grid: no fog of war, cutaway or room culling */
    arc_camera cam = ed_cam3d(); arc_set_camera(&cam);
    int W = E.d.width, H = E.d.height; float c = ed_cell(); arc_v3 o = E.d.origin;
    arc_editor_part parts[ARC_EDITOR_PARTS];
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        int n = E.d.tile_mesh(x, y, ed_get_raw(x, y), parts, ARC_EDITOR_PARTS, E.d.user);
        if (n <= 0) continue;
        arc_m4 at = arc_m4_mul(arc_m4_translate(arc_v3_make(o.x + ((float)x + 0.5f) * c, o.y, o.z + ((float)y + 0.5f) * c)), arc_m4_scale(arc_v3_make(c, c, c)));
        for (int k = 0; k < n && k < ARC_EDITOR_PARTS; k++) if (parts[k].mesh.id) arc_draw_mesh(parts[k].mesh, arc_m4_mul(at, parts[k].model), parts[k].tint ? parts[k].tint : 0xFFFFFF);
    }
    for (int i = 0; i < ed_nobj(); i++) {   /* 0.10.8 objects: the game's meshes, else a box of its colour */
        int ox = arc_level_object_x(LV(), E.s.layer, i), oy = arc_level_object_y(LV(), E.s.layer, i);
        int n = E.d.object_mesh ? E.d.object_mesh(E.s.layer, i, parts, ARC_EDITOR_PARTS, E.d.user) : 0;
        arc_m4 at = arc_m4_mul(arc_m4_translate(arc_v3_make(o.x + ((float)ox + 0.5f) * c, o.y, o.z + ((float)oy + 0.5f) * c)), arc_m4_scale(arc_v3_make(c, c, c)));
        for (int k = 0; k < n && k < ARC_EDITOR_PARTS; k++) if (parts[k].mesh.id) arc_draw_mesh(parts[k].mesh, arc_m4_mul(at, parts[k].model), parts[k].tint ? parts[k].tint : 0xFFFFFF);
        if (n <= 0) { const arc_editor_object *kd = ed_kind(arc_level_object_kind(LV(), E.s.layer, i)); ed_box3d(ox, oy, ox, oy, 0.6f, kd ? kd->rgb : 0xFF40FF); }
        if (i == E.s.osel) ed_box3d(ox, oy, ox, oy, 1.1f, 0xFFE060);
    }
    float yy = o.y + c * 0.01f;
    if (!E.s.nogrid) {
        for (int x = 0; x <= W; x++) arc_draw_line(arc_v3_make(o.x + (float)x * c, yy, o.z), arc_v3_make(o.x + (float)x * c, yy, o.z + (float)H * c), 0x406080);
        for (int y = 0; y <= H; y++) arc_draw_line(arc_v3_make(o.x, yy, o.z + (float)y * c), arc_v3_make(o.x + (float)W * c, yy, o.z + (float)y * c), 0x406080);
    }
    if (E.s.dragging && E.s.tool == ED_RECT) ed_box3d(arc_mini(E.s.sx, E.s.lx), arc_mini(E.s.sy, E.s.ly), arc_maxi(E.s.sx, E.s.lx), arc_maxi(E.s.sy, E.s.ly), 0.1f, 0xFFE060);
    else if (E.s.dragging && E.s.tool == ED_LINE) {
        int x0 = E.s.sx, y0 = E.s.sy, x1 = E.s.lx, y1 = E.s.ly, dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy, guard = 0;
        for (;;) { ed_box3d(x0, y0, x0, y0, 0.1f, 0xFFE060); if ((x0 == x1 && y0 == y1) || ++guard > 100000) break; int e2 = 2 * err; if (e2 >= dy) { err += dy; x0 += sx; } if (e2 <= dx) { err += dx; y0 += sy; } }
    }
    if (E.s.hover_ok) ed_box3d(E.s.hx, E.s.hy, E.s.hx, E.s.hy, 1.05f, 0xFFFFFF);
    arc_viewport_end();
    arc__view_effects_off(false);   /* (after the viewport's pass ended: a pass draws with its last settings) */
}
void arc__editor_render(void) {
    if (!E.begun || !E.s.open || E.s.zoom <= 0) return;
    arc_blend bl = arc_get_blend(); arc_set_blend(ARC_BLEND_ALPHA);
    ed_geom g = ed_layout();
    int W = E.d.width, H = E.d.height;
    bool own = E.s.view3d == ARC_EDITOR_VIEW_3D && ed_hasown();
    bool in3d = E.s.view3d == ARC_EDITOR_VIEW_GAME && ed_has3d();
    int sel_id = E.tiles[arc_clampi(E.s.sel, 0, E.d.tile_count - 1)].id;
    if (own) ed_render3d(g);
    else if (!in3d) {
        float z = E.s.zoom, inset = (!E.s.nogrid && z >= 6) ? 1.0f : 0.0f;
        arc_draw_rect(g.ax, g.ay, g.aw, g.ah, arc_rgba(0x0C1014, 0.92f));
        arc_set_clip(g.ax, g.ay, g.aw, g.ah);
        if (inset > 0) arc_draw_rect(E.s.panx, E.s.pany, z * (float)W + 1, z * (float)H + 1, 0x383E48);   /* grid lines show between cells */
        int x0 = arc_maxi(0, (int)floorf((g.ax - E.s.panx) / z)), x1 = arc_mini(W - 1, (int)floorf((g.ax + g.aw - E.s.panx) / z));
        int y0 = arc_maxi(0, (int)floorf((g.ay - E.s.pany) / z)), y1 = arc_mini(H - 1, (int)floorf((g.ay + g.ah - E.s.pany) / z));
        for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++)
            ed_draw_cell(ed_get_raw(x, y), E.s.panx + (float)x * z, E.s.pany + (float)y * z, z, inset);
        int ol = ed_onion_layer();
        if (ol >= 0) {   /* 0.10.8 onion skin: the layer above or below, faint, where it isn't the erase tile (floor) */
            int erase = E.tiles[E.d.erase_tile].id;
            for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
                int v = ed_get_layer(ol, x, y); if (v == erase) continue;
                int t = ed_tile_of(v); uint32_t col = t >= 0 ? (E.tiles[t].rgb & 0xFFFFFF) : 0xFF00FF;
                arc_draw_rect(E.s.panx + (float)x * z + z * 0.25f, E.s.pany + (float)y * z + z * 0.25f, z * 0.5f, z * 0.5f, arc_rgba(col, 0.45f));
            }
            for (int i = 0; LV() && i < arc_level_object_count(LV(), ol); i++) {
                int ox = arc_level_object_x(LV(), ol, i), oy = arc_level_object_y(LV(), ol, i);
                if (ox >= x0 && ox <= x1 && oy >= y0 && oy <= y1) ed_draw_object(LV(), ol, i, E.s.panx + (float)ox * z, E.s.pany + (float)oy * z, z, 0.35f, false);
            }
        }
        for (int i = 0; i < ed_nobj(); i++) {   /* 0.10.8 objects */
            int ox = arc_level_object_x(LV(), E.s.layer, i), oy = arc_level_object_y(LV(), E.s.layer, i);
            if (E.s.omoving && i == E.s.osel) { ox = E.s.lx; oy = E.s.ly; }
            if (ox >= x0 && ox <= x1 && oy >= y0 && oy <= y1) ed_draw_object(LV(), E.s.layer, i, E.s.panx + (float)ox * z, E.s.pany + (float)oy * z, z, 1.0f, i == E.s.osel);
        }
        if (E.s.has_sel) {   /* 0.10.8 the selection */
            float sx = E.s.panx + (float)E.s.s0x * z, sy = E.s.pany + (float)E.s.s0y * z, sw = (float)(E.s.s1x - E.s.s0x + 1) * z, sh = (float)(E.s.s1y - E.s.s0y + 1) * z;
            arc_draw_rect(sx, sy, sw, sh, arc_rgba(0x60C0FF, 0.18f)); arc_draw_rect_lines(sx, sy, sw + 1, sh + 1, 0x60C0FF);
        }
        if (E.s.pasting && E.s.hover_ok && E.ccells) {   /* 0.10.8 the clipboard at the pointer */
            for (int cy = 0; cy < E.ch; cy++) for (int cx = 0; cx < E.cw; cx++) {
                int t = ed_tile_of(E.ccells[cy * E.cw + cx]); uint32_t col = t >= 0 ? (E.tiles[t].rgb & 0xFFFFFF) : 0xFF00FF;
                arc_draw_rect(E.s.panx + (float)(E.s.hx + cx) * z, E.s.pany + (float)(E.s.hy + cy) * z, z, z, arc_rgba(col, 0.6f));
            }
            for (int i = 0; E.clip && i < arc_level_object_count(E.clip, 0); i++)
                ed_draw_object(E.clip, 0, i, E.s.panx + (float)(E.s.hx + arc_level_object_x(E.clip, 0, i)) * z, E.s.pany + (float)(E.s.hy + arc_level_object_y(E.clip, 0, i)) * z, z, 0.8f, false);
            arc_draw_rect_lines(E.s.panx + (float)E.s.hx * z, E.s.pany + (float)E.s.hy * z, (float)E.cw * z + 1, (float)E.ch * z + 1, 0xFFE060);
        }
        if (E.s.dragging && E.s.tool == ED_RECT) {
            int ax = arc_mini(E.s.sx, E.s.lx), bx = arc_maxi(E.s.sx, E.s.lx), ay = arc_mini(E.s.sy, E.s.ly), by = arc_maxi(E.s.sy, E.s.ly);
            arc_draw_rect(E.s.panx + (float)ax * z, E.s.pany + (float)ay * z, (float)(bx - ax + 1) * z, (float)(by - ay + 1) * z, arc_rgba(0xFFFFFF, 0.25f));
            arc_draw_rect_lines(E.s.panx + (float)ax * z, E.s.pany + (float)ay * z, (float)(bx - ax + 1) * z + 1, (float)(by - ay + 1) * z + 1, 0xFFE060);
        } else if (E.s.dragging && E.s.tool == ED_LINE) ed_preview_line(E.s.sx, E.s.sy, E.s.lx, E.s.ly, 0xFFE060, false);
        if (E.s.hover_ok) ed_cell_outline(E.s.hx, E.s.hy, 0xFFFFFF);
        arc_clear_clip();
    } else {
        float c = E.d.world_cell; arc_v3 o = E.d.origin; float yy = o.y + c * 0.02f;
        if (!E.s.nogrid) {
            for (int x = 0; x <= W; x++) arc_draw_line(arc_v3_make(o.x + (float)x * c, yy, o.z), arc_v3_make(o.x + (float)x * c, yy, o.z + (float)H * c), 0x60A0FF);
            for (int y = 0; y <= H; y++) arc_draw_line(arc_v3_make(o.x, yy, o.z + (float)y * c), arc_v3_make(o.x + (float)W * c, yy, o.z + (float)y * c), 0x60A0FF);
        }
        if (E.s.dragging && E.s.tool == ED_RECT) {
            int ax = arc_mini(E.s.sx, E.s.lx), bx = arc_maxi(E.s.sx, E.s.lx), ay = arc_mini(E.s.sy, E.s.ly), by = arc_maxi(E.s.sy, E.s.ly);
            arc_draw_box_lines(arc_v3_make(o.x + (float)ax * c, o.y, o.z + (float)ay * c), arc_v3_make(o.x + (float)(bx + 1) * c, o.y + c * 0.1f, o.z + (float)(by + 1) * c), 0xFFE060);
        } else if (E.s.dragging && E.s.tool == ED_LINE) ed_preview_line(E.s.sx, E.s.sy, E.s.lx, E.s.ly, 0xFFE060, true);
        if (E.s.hover_ok) arc_draw_box_lines(arc_v3_make(o.x + (float)E.s.hx * c, o.y, o.z + (float)E.s.hy * c), arc_v3_make(o.x + (float)(E.s.hx + 1) * c, o.y + c * 0.25f, o.z + (float)(E.s.hy + 1) * c), 0xFFFFFF);
        for (int i = 0; i < ed_nobj(); i++) {   /* 0.10.8 */
            int ox = arc_level_object_x(LV(), E.s.layer, i), oy = arc_level_object_y(LV(), E.s.layer, i); const arc_editor_object *kd = ed_kind(arc_level_object_kind(LV(), E.s.layer, i));
            arc_draw_box_lines(arc_v3_make(o.x + ((float)ox + 0.2f) * c, o.y, o.z + ((float)oy + 0.2f) * c), arc_v3_make(o.x + ((float)ox + 0.8f) * c, o.y + c * 0.6f, o.z + ((float)oy + 0.8f) * c), i == E.s.osel ? 0xFFE060 : kd ? kd->rgb : 0xFF40FF);
        }
    }
    /* palette panel */
    float H_ = (float)arc_height(), pad = 4.0f * g.k, ts = g.ts, ty = (g.rh - 8.0f * ts) * 0.5f;
    arc_draw_rect(g.px, 0, g.pw, H_, arc_rgba(0x181C24, 0.95f));
    arc_draw_rect(g.px, 0, g.pw, g.rh + pad, 0x2C3E58);
    arc_set_clip(g.px, 0, g.pw, H_);
    arc_draw_textf(g.px + pad, pad * 0.5f + ty, ts, 0xE8ECF4, "%s%s", E.d.name ? E.d.name : "Editor", E.s.dirty ? " *" : "");
    arc_clear_clip();
    for (int i = 0; i < ed_nboxes; i++) {
        const ed_box *b = &ed_boxes[i];
        bool on = (b->kind == 0 && b->index == E.s.tool) || (b->kind == 2 && b->index == E.s.sel) || (b->kind == 1 && b->index == ED_A_PLAY && E.s.playing);
        if (b->kind >= 3) {   /* 0.10.8 object kinds, fields, tabs, delete */
            if (b->kind == 3) {
                const arc_editor_object *k = &E.objs[b->index];
                if (b->index == E.s.okind) arc_draw_rect(b->x, b->y, b->w, b->h, 0x4E6A96);
                float sw = b->h - 2 * g.k; uint32_t rgb = k->rgb & 0xFFFFFF;
                arc_draw_rect(b->x + g.k * 2, b->y + g.k * 2, sw - 2 * g.k, sw - 2 * g.k, rgb);
                const char *nm = k->name ? k->name : k->kind; float sc = ts; while (sc > 1 && arc_text_width(sc, nm) > b->w - sw - 6 * g.k) sc -= 0.5f;
                arc_set_clip(b->x, b->y, b->w, b->h); arc_draw_text(b->x + sw + 3 * g.k, b->y + (b->h - 8 * sc) * 0.5f, sc, 0xE8ECF4, nm); arc_clear_clip();
                if (k->key) { const char *kn = arc_key_name(k->key); arc_draw_text(b->x + b->w - arc_text_width(ts * 0.75f, kn) - 2 * g.k, b->y + (b->h - 6 * ts) * 0.5f, ts * 0.75f, 0xFFD060, kn); }
            } else if (b->kind == 5) {
                bool on5 = b->index == E.s.tab; arc_draw_rect(b->x, b->y, b->w, b->h, on5 ? 0x4E6A96 : 0x2A303C);
                const char *lab = ed_tab_names[b->index]; float sc = ts; while (sc > 1 && arc_text_width(sc, lab) > b->w - 4) sc -= 0.5f;
                arc_draw_text(b->x + arc_maxf(2.0f, (b->w - arc_text_width(sc, lab)) * 0.5f), b->y + (b->h - 8 * sc) * 0.5f, sc, 0xE8ECF4, lab);
            } else if (b->kind == 6) {
                arc_draw_rect(b->x, b->y, b->w, b->h, 0x5A2A2A); const char *lab = "Delete DEL";
                arc_draw_text(b->x + arc_maxf(2.0f, (b->w - arc_text_width(ts, lab)) * 0.5f), b->y + (b->h - 8 * ts) * 0.5f, ts, 0xF0D0D0, lab);
            } else {   /* a field: its label (once, with the value box), - value + */
                const arc_editor_field *f = ed_row_field(E.s.tab, b->index);
                if (!f) continue;
                bool editing = E.s.edit_row == b->index && E.s.edit_tab == E.s.tab;
                arc_draw_rect(b->x, b->y, b->w, b->h, b->part == 1 ? (editing ? 0x34507A : 0x20262E) : 0x2A303C);
                char txt[160];
                if (b->part == 0) snprintf(txt, sizeof txt, "-"); else if (b->part == 2) snprintf(txt, sizeof txt, "+");
                else if (editing) snprintf(txt, sizeof txt, "%s_", E.s.ebuf); else ed_row_text(E.s.tab, b->index, txt, sizeof txt);
                float sc = ts; while (sc > 1 && arc_text_width(sc, txt) > b->w - 4) sc -= 0.5f;
                arc_set_clip(b->x, b->y, b->w, b->h);
                arc_draw_text(b->part == 1 ? b->x + 3 * g.k : b->x + arc_maxf(1.0f, (b->w - arc_text_width(sc, txt)) * 0.5f), b->y + (b->h - 8 * sc) * 0.5f, sc, 0xE8ECF4, txt);
                arc_clear_clip();
                if (b->part == 1) {
                    float lx = g.px + pad, lw = b->x - lx - (f->type == ARC_FIELD_TEXT || f->type == ARC_FIELD_BOOL ? 0 : g.rh) - 2 * g.k;
                    float lsc = ts; while (lsc > 1 && arc_text_width(lsc, f->name) > lw) lsc -= 0.5f;
                    arc_set_clip(lx, b->y, arc_maxf(1.0f, lw), b->h); arc_draw_text(lx, b->y + (b->h - 8 * lsc) * 0.5f, lsc, 0xA8B0C0, f->name); arc_clear_clip();
                }
            }
            continue;
        }
        if (b->kind < 2) {
            bool en = b->kind == 0 || (b->index == ED_A_UNDO ? arc_editor_undo_steps() > 0 : b->index == ED_A_REDO ? ed_redo_steps() > 0 : b->index == ED_A_SAVE ? (E.d.save != NULL || LV() != NULL) : b->index == ED_A_LOAD ? (E.d.load != NULL || LV() != NULL) : b->index == ED_A_TEST ? E.d.playtest != NULL : true);
            arc_draw_rect(b->x, b->y, b->w, b->h, on ? 0x4E6A96 : 0x2A303C);
            const char *lab = b->kind == 0 ? ed_tool_labels[b->index] : (b->index == ED_A_PLAY && E.s.playing) ? "Pause P" : ed_action_labels[b->index];
            float sc = ts; while (sc > 1 && arc_text_width(sc, lab) > b->w - 4) sc -= 0.5f;
            arc_set_clip(b->x, b->y, b->w, b->h);   /* (a small window: labels are cut, not spilled) */
            arc_draw_text(b->x + arc_maxf(2.0f, (b->w - arc_text_width(sc, lab)) * 0.5f), b->y + (b->h - 8 * sc) * 0.5f, sc, en ? 0xE8ECF4 : 0x687080, lab);
            arc_clear_clip();
        } else {
            const arc_editor_tile *t = &E.tiles[b->index];
            if (on) arc_draw_rect(b->x, b->y, b->w, b->h, 0x4E6A96);
            float sw = b->h - 2 * g.k;
            ed_draw_cell(t->id, b->x + g.k, b->y + g.k, sw, 0);
            char lab[64];
            if (E.csize == 1 && t->id > 32 && t->id < 127) snprintf(lab, sizeof lab, "%c %s", (char)t->id, t->name ? t->name : "");
            else snprintf(lab, sizeof lab, "%d %s", t->id, t->name ? t->name : "");
            float sc = ts; float avail = b->w - sw - 4 * g.k - (t->key ? arc_text_width(ts * 0.75f, arc_key_name(t->key)) + 4 * g.k : 0);
            while (sc > 1 && arc_text_width(sc, lab) > avail) sc -= 0.5f;
            arc_set_clip(b->x, b->y, b->w, b->h);
            arc_draw_text(b->x + sw + 4 * g.k, b->y + (b->h - 8 * sc) * 0.5f, sc, 0xE8ECF4, lab);
            arc_clear_clip();
            if (t->key) { const char *kn = arc_key_name(t->key); arc_draw_text(b->x + b->w - arc_text_width(ts * 0.75f, kn) - 2 * g.k, b->y + (b->h - 6 * ts) * 0.5f, ts * 0.75f, 0xFFD060, kn); }
        }
    }
    /* status line */
    float sy = g.ah;
    arc_draw_rect(0, sy, g.aw, g.sh, arc_rgba(0x101418, 0.95f));
    char hover[48] = "";
    if (E.s.hover_ok) {
        int v = ed_get_raw(E.s.hx, E.s.hy), t = ed_tile_of(v);
        if (E.csize == 1 && v > 32 && v < 127) snprintf(hover, sizeof hover, "%d,%d '%c' %s", E.s.hx, E.s.hy, (char)v, t >= 0 && E.tiles[t].name ? E.tiles[t].name : "?");
        else snprintf(hover, sizeof hover, "%d,%d %d %s", E.s.hx, E.s.hy, v, t >= 0 && E.tiles[t].name ? E.tiles[t].name : "?");
    }
    const arc_editor_tile *st = &E.tiles[arc_clampi(E.s.sel, 0, E.d.tile_count - 1)]; (void)sel_id;
    float x = 4 * g.k, yy2 = sy + (g.sh - 8 * ts) * 0.5f;
    arc_set_clip(0, sy, g.aw, g.sh);
    x += arc_draw_textf(x, yy2, ts, E.s.playing ? 0x60FF60 : 0xFFD060, "%s", E.s.playing ? "PLAYING" : "PAUSED") + 10 * g.k;
    x += arc_draw_textf(x, yy2, ts, 0xE8ECF4, "%s  %s  %s", ed_tool_names[E.s.tool], st->name ? st->name : "", hover) + 10 * g.k;
    if (E.d.layer_count > 0) {   /* 0.10.3 */
        const char *ln = LV() ? arc_level_layer_name(LV(), E.s.layer) : E.d.layer_names && E.d.layer_names[E.s.layer] ? E.d.layer_names[E.s.layer] : NULL;
        x += (ln ? arc_draw_textf(x, yy2, ts, 0x90D0FF, "%s (%d/%d) PGUP/PGDN", ln, E.s.layer + 1, E.d.layer_count) : arc_draw_textf(x, yy2, ts, 0x90D0FF, "layer %d/%d PGUP/PGDN", E.s.layer + 1, E.d.layer_count)) + 10 * g.k;
    }
    if (own) x += arc_draw_text(x, yy2, ts, 0x90D0FF, "3D") + 10 * g.k;
    { int ol = ed_onion_layer();   /* 0.10.8 */
      if (ol >= 0) { const char *on = LV() ? arc_level_layer_name(LV(), ol) : (E.d.layer_names && E.d.layer_names[ol] ? E.d.layer_names[ol] : ""); x += arc_draw_textf(x, yy2, ts, 0xC0A0FF, "onion %s %s", E.s.onion < 0 ? "above" : "below", on) + 10 * g.k; }
      if (E.s.pasting) x += arc_draw_textf(x, yy2, ts, 0xFFE060, "PASTE %dx%d  R turn  F flip  click place", E.cw, E.ch) + 10 * g.k;
      else if (E.s.has_sel) x += arc_draw_textf(x, yy2, ts, 0x60C0FF, "sel %dx%d", E.s.s1x - E.s.s0x + 1, E.s.s1y - E.s.s0y + 1) + 10 * g.k;
      if (E.s.osel >= 0 && E.s.osel < ed_nobj()) x += arc_draw_textf(x, yy2, ts, 0xFFE060, "object %d %s", E.s.osel, arc_level_object_kind(LV(), E.s.layer, E.s.osel)) + 10 * g.k; }
    x += arc_draw_textf(x, yy2, ts, 0xA0A8B8, "undo %d redo %d  %dx%d %.0fpx", arc_editor_undo_steps(), ed_redo_steps(), W, H, E.s.zoom) + 10 * g.k;
    if (E.msg[0]) arc_draw_text(x, yy2, ts, 0xFF7060, E.msg);
    arc_clear_clip();
    arc_set_blend(bl);
}

/* ---- state snapshots (seek / rewind / state_save): only once an editor has begun ---- */
#define ED_MAGIC 0x38314445u      /* "ED18" (0.10.8: objects' undo records and the clipboard follow; the state grew) */
#define ED_MAGIC_13 0x33314445u   /* "ED13" (0.10.3 .. 0.10.7: read, the new parts empty) */
size_t arc__editor_state(arc_writer *w) {
    if (!E.begun) return w->len;
    uint32_t magic = ED_MAGIC; uint64_t n = E.end - E.base, top = E.top - E.base;
    arc_write(w, &magic, 4); arc_write(w, &E.s, sizeof E.s); arc_write(w, E.msg, sizeof E.msg);
    arc_write(w, &n, 8); arc_write(w, &top, 8);
    for (uint64_t i = E.base; i < E.end; i++) arc_write(w, ed_at(i), sizeof(ed_rec));
    uint64_t on = E.oend - E.obase, otop = E.otop - E.obase;   /* 0.10.8 */
    arc_write(w, &on, 8); arc_write(w, &otop, 8);
    for (uint64_t i = E.obase; i < E.oend; i++) arc_write(w, ed_oat(i), sizeof(ed_orec));
    int32_t cw = E.ccells ? E.cw : 0, ch = E.ccells ? E.ch : 0; arc_write(w, &cw, 4); arc_write(w, &ch, 4);
    if (cw && ch) arc_write(w, E.ccells, sizeof(int32_t) * (size_t)(cw * ch));
    size_t tl = 0; char *t = E.clip ? arc_level_text(E.clip, &tl) : NULL; uint32_t tl32 = (uint32_t)tl;
    arc_write(w, &tl32, 4); if (tl32) arc_write(w, t, tl); free(t);
    return w->len;
}
bool arc__editor_state_read(arc_reader *r, bool commit) {
    if (r->pos >= r->size) {                       /* a state saved before the editor began: closed, no history */
        if (commit && E.begun) { memset(&E.s, 0, sizeof E.s); E.s.osel = -1; E.s.edit_row = -1; E.base = E.top = E.end = 0; E.obase = E.otop = E.oend = 0; E.too_big = 0; ed_validate(); }
        return true;
    }
    uint32_t magic = 0; memcpy(&magic, r->data + r->pos, r->size - r->pos >= 4 ? 4 : 0);
    if (magic == 0x3154454Eu || magic == 0x31564153u || magic == 0x31544853u) {   /* (0.10.0: or the shots' block) */   /* 0.8: no editor block, the net layer's follows (0.8.1: or the loads' block) */
        if (commit && E.begun) { memset(&E.s, 0, sizeof E.s); E.s.osel = -1; E.s.edit_row = -1; E.base = E.top = E.end = 0; E.obase = E.otop = E.oend = 0; E.too_big = 0; ed_validate(); }
        return true;
    }
    if (magic != ED_MAGIC && magic != ED_MAGIC_13) { r->fail = true; return false; }
    bool v13 = magic == ED_MAGIC_13; size_t ssz = v13 ? offsetof(ed_state, tab) : sizeof(ed_state);
    ed_state s; memset(&s, 0, sizeof s); s.osel = -1; s.edit_row = -1;
    char msg[sizeof E.msg]; uint64_t n = 0, top = 0;
    if (!arc_read(r, &magic, 4) || !arc_read(r, &s, ssz) || !arc_read(r, msg, sizeof msg) || !arc_read(r, &n, 8) || !arc_read(r, &top, 8)) return false;
    if (v13) { s.osel = -1; s.edit_row = -1; }
    if (top > n || (r->size - r->pos) / sizeof(ed_rec) < n) { r->fail = true; return false; }
    size_t cells_at = r->pos; r->pos += (size_t)n * sizeof(ed_rec);
    uint64_t on = 0, otop = 0; size_t orecs_at = r->pos; int32_t cw = 0, ch = 0; size_t ccells_at = 0; uint32_t tl = 0; size_t text_at = 0;
    if (!v13) {   /* 0.10.8 parts */
        if (!arc_read(r, &on, 8) || !arc_read(r, &otop, 8)) return false;
        if (otop > on || on > ED_OCAP || (r->size - r->pos) / sizeof(ed_orec) < on) { r->fail = true; return false; }
        orecs_at = r->pos; r->pos += (size_t)on * sizeof(ed_orec);
        if (!arc_read(r, &cw, 4) || !arc_read(r, &ch, 4) || cw < 0 || ch < 0 || cw > 4096 || ch > 4096) { r->fail = true; return false; }
        ccells_at = r->pos; if ((size_t)(r->size - r->pos) < sizeof(int32_t) * (size_t)(cw * ch)) { r->fail = true; return false; }
        r->pos += sizeof(int32_t) * (size_t)(cw * ch);
        if (!arc_read(r, &tl, 4) || (size_t)(r->size - r->pos) < tl) { r->fail = true; return false; }
        text_at = r->pos; r->pos += tl;
    }
    if (!E.begun) return true;                                  /* the editor has ended since: its part skipped */
    if (n > (uint64_t)E.cap) { r->fail = true; return false; }
    if (!commit) return true;
    E.s = s; memcpy(E.msg, msg, sizeof msg); E.msg[sizeof E.msg - 1] = 0;
    if (E.d.layer_count) ed_use_layer(E.s.layer); else E.s.layer = 0;   /* 0.10.3 */
    memcpy(E.ring, r->data + cells_at, (size_t)n * sizeof(ed_rec));
    E.base = 0; E.top = top; E.end = n;
    E.obase = E.otop = E.oend = 0;
    if (E.oring && on) { memcpy(E.oring, r->data + orecs_at, (size_t)on * sizeof(ed_orec)); E.otop = otop; E.oend = on; }
    free(E.ccells); E.ccells = NULL; arc_level_free(E.clip); E.clip = NULL; E.cw = E.ch = 0;
    if (cw && ch) { E.cw = cw; E.ch = ch; E.ccells = (int32_t *)arc__xalloc(sizeof(int32_t) * (size_t)(cw * ch)); memcpy(E.ccells, r->data + ccells_at, sizeof(int32_t) * (size_t)(cw * ch)); }
    if (tl) E.clip = arc_level_parse((const char *)r->data + text_at, tl, "clipboard");
    return !r->fail;
}

/* ---- agent commands ---- */
static bool ed_parse_id(const char *s, int *id) {
    if (!s || !*s) return false;
    if (s[0] == '=' && s[1]) { *id = atoi(s + 1); return true; }
    if (!s[1]) { *id = (unsigned char)s[0]; return true; }
    for (int i = 0; i < E.d.tile_count; i++) {
        const char *n = E.tiles[i].name; if (!n) continue;
        size_t k = 0; while (n[k] && s[k] && tolower((unsigned char)n[k]) == tolower((unsigned char)s[k])) k++;
        if (!n[k] && !s[k]) { *id = E.tiles[i].id; return true; }
    }
    return false;
}
void arc__editor_dump_json(arc_json *j) {
    if (!E.begun) return;
    arc_json_obj_k(j, "editor");
    arc_json_bool_k(j, "open", E.s.open != 0); arc_json_bool_k(j, "playing", E.s.playing != 0); arc_json_bool_k(j, "dirty", E.s.dirty != 0);
    arc_json_str_k(j, "tool", ed_tool_names[E.s.tool]); arc_json_int_k(j, "tile", E.tiles[arc_clampi(E.s.sel, 0, E.d.tile_count - 1)].id);
    arc_json_int_k(j, "undo", arc_editor_undo_steps()); arc_json_int_k(j, "redo", ed_redo_steps());
    arc_json_str_k(j, "message", E.msg);
    static const char *VN[] = { "2d", "game", "3d" };
    arc_json_str_k(j, "view", VN[E.s.view3d % 3]); arc_json_int_k(j, "layer", E.s.layer); arc_json_int_k(j, "layers", E.d.layer_count);   /* 0.10.3 */
    arc_json_bool_k(j, "hover", E.s.hover_ok != 0); arc_json_int_k(j, "hover_x", E.s.hx); arc_json_int_k(j, "hover_y", E.s.hy);
    /* 0.10.8 */
    arc_json_str_k(j, "tab", ed_tab_names[arc_clampi(E.s.tab, 0, ED_TABS - 1)]);
    arc_json_int_k(j, "onion", ed_onion_layer());
    arc_json_bool_k(j, "pasting", E.s.pasting != 0);
    if (E.s.has_sel) { arc_json_arr_k(j, "selection"); arc_json_int(j, E.s.s0x); arc_json_int(j, E.s.s0y); arc_json_int(j, E.s.s1x); arc_json_int(j, E.s.s1y); arc_json_end(j); }
    if (E.ccells) { arc_json_arr_k(j, "clipboard"); arc_json_int(j, E.cw); arc_json_int(j, E.ch); arc_json_end(j); }
    if (LV()) {
        arc_json_str_k(j, "file", E.lpath); arc_json_str_k(j, "layer_name", arc_level_layer_name(LV(), E.s.layer));
        if (E.d.object_count) arc_json_str_k(j, "object_kind", E.objs[arc_clampi(E.s.okind, 0, E.d.object_count - 1)].kind);
        arc_json_int_k(j, "object_selected", arc_editor_object_selected());
        arc_json_obj_k(j, "settings");
        for (int i = 0; i < arc_level_setting_count(LV(), E.s.layer); i++) { const char *k = arc_level_setting_key(LV(), E.s.layer, i); arc_json_str_k(j, k, arc_level_get(LV(), E.s.layer, k, "")); }
        arc_json_end(j);
        arc_json_arr_k(j, "objects");
        for (int i = 0; i < ed_nobj(); i++) {
            arc_json_obj(j); arc_json_int_k(j, "i", i); arc_json_str_k(j, "kind", arc_level_object_kind(LV(), E.s.layer, i));
            arc_json_int_k(j, "x", arc_level_object_x(LV(), E.s.layer, i)); arc_json_int_k(j, "y", arc_level_object_y(LV(), E.s.layer, i));
            arc_json_obj_k(j, "fields");
            for (int k = 0; k < arc_level_object_field_count(LV(), E.s.layer, i); k++) { const char *key = arc_level_object_field_key(LV(), E.s.layer, i, k); arc_json_str_k(j, key, arc_level_object_get(LV(), E.s.layer, i, key, "")); }
            arc_json_end(j); arc_json_end(j);
        }
        arc_json_end(j);
    }
    arc_json_end(j);
}
void arc__editor_command(int argc, const char **argv) {
#define ED_OK(...) do { printf(__VA_ARGS__); fflush(stdout); } while (0)
    if (!E.begun) { ED_OK("ARC error editor: the game has not called arc_editor_begin\n"); return; }
    const char *sub = argc > 1 ? argv[1] : "";
    int id = 0;
    if (!*sub || !strcmp(sub, "on") || !strcmp(sub, "off")) {
        arc_editor_open(*sub ? !strcmp(sub, "on") : !E.s.open);
        ED_OK("ARC ok editor %s\n", E.s.open ? "on" : "off"); return;
    }
    if (!strcmp(sub, "paint") && argc >= 5 && ed_parse_id(argv[4], &id)) { bool r = arc_editor_paint(atoi(argv[2]), atoi(argv[3]), id); ED_OK("ARC ok editor paint %d\n", r ? 1 : 0); return; }
    if (!strcmp(sub, "fill") && argc >= 7 && ed_parse_id(argv[6], &id)) { int n = arc_editor_fill(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), id); ED_OK("ARC ok editor fill %d\n", n); return; }
    if (!strcmp(sub, "line") && argc >= 7 && ed_parse_id(argv[6], &id)) { int n = arc_editor_line(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), id); ED_OK("ARC ok editor line %d\n", n); return; }
    if (!strcmp(sub, "flood") && argc >= 5 && ed_parse_id(argv[4], &id)) { int n = arc_editor_flood(atoi(argv[2]), atoi(argv[3]), id); ED_OK("ARC ok editor flood %d\n", n); return; }
    if (!strcmp(sub, "undo")) { bool r = arc_editor_undo(); ED_OK(r ? "ARC ok editor undo\n" : "ARC error editor nothing to undo\n"); return; }
    if (!strcmp(sub, "redo")) { bool r = arc_editor_redo(); ED_OK(r ? "ARC ok editor redo\n" : "ARC error editor nothing to redo\n"); return; }
    if (!strcmp(sub, "save")) { if (!E.d.save && !LV()) ED_OK("ARC error editor the game gave no save hook\n"); else arc_editor_save(); return; }
    if (!strcmp(sub, "load")) { bool r = arc_editor_load(); ED_OK(r ? "ARC ok editor load\n" : "ARC error editor load failed\n"); return; }
    if (!strcmp(sub, "play") || !strcmp(sub, "pause")) { E.s.playing = !strcmp(sub, "play"); ED_OK("ARC ok editor %s\n", sub); return; }
    if (!strcmp(sub, "tile") && argc >= 3 && ed_parse_id(argv[2], &id)) {
        int t = ed_tile_of(id); if (t < 0) { ED_OK("ARC error editor no tile %s\n", argv[2]); return; }
        E.s.sel = t; ED_OK("ARC ok editor tile %d\n", id); return;
    }
    if (!strcmp(sub, "tool") && argc >= 3) {
        for (int t = 0; t < ED_TOOLS; t++) if (!strcmp(argv[2], ed_tool_names[t])) { E.s.tool = (uint8_t)t; E.s.dragging = 0; ED_OK("ARC ok editor tool %s\n", argv[2]); return; }
        ED_OK("ARC error editor tool: brush line rect fill pick select\n"); return;
    }
    if (!strcmp(sub, "layer") && argc >= 3) {   /* 0.10.3 */
        if (!E.d.layer_count) { ED_OK("ARC error editor: no layers\n"); return; }
        arc_editor_set_layer(atoi(argv[2])); ED_OK("ARC ok editor layer %d\n", E.s.layer); return;
    }
    if (!strcmp(sub, "view") && argc >= 3) {
        int v = !strcmp(argv[2], "3d") ? ARC_EDITOR_VIEW_3D : !strcmp(argv[2], "game") ? ARC_EDITOR_VIEW_GAME : ARC_EDITOR_VIEW_2D;
        arc_editor_set_view(v);
        if (E.s.view3d != v) { ED_OK("ARC error editor view %s: %s\n", argv[2], v == ARC_EDITOR_VIEW_3D ? "the game gave no tile_mesh" : "the game gave no camera / world_cell"); return; }
        if (v == ARC_EDITOR_VIEW_3D && E.s.dist <= 0) ed_fit3d();
        ED_OK("ARC ok editor view %s\n", argv[2]); return;
    }
    if (!strcmp(sub, "orbit") && argc >= 5) {
        if (E.s.dist <= 0) ed_fit3d();
        E.s.yaw = (float)atof(argv[2]) * 0.0174532925f; E.s.pitch = arc_clampf((float)atof(argv[3]) * 0.0174532925f, 0.15f, 1.55f); E.s.dist = arc_maxf(0.5f, (float)atof(argv[4]) * ed_cell());
        if (argc >= 7) { E.s.tx = E.d.origin.x + (float)atof(argv[5]) * ed_cell(); E.s.tz = E.d.origin.z + (float)atof(argv[6]) * ed_cell(); }
        ED_OK("ARC ok editor orbit\n"); return;
    }
    /* 0.10.8 objects, settings, selection and clipboard, onion skin, test-play */
    if (!strcmp(sub, "object")) {
        const char *op = argc > 2 ? argv[2] : "";
        if (!LV()) { ED_OK("ARC error editor object: the game gave no level (arc_editor_desc.level)\n"); return; }
        if (!strcmp(op, "add") && argc >= 6) {
            int x = atoi(argv[3]), y = atoi(argv[4]);
            if (E.d.object_count && ed_kind_index(argv[5]) < 0) { ED_OK("ARC error editor object add: no object kind \"%s\"\n", argv[5]); return; }
            uint32_t g = ed_new_group(); int i = ed_obj_add(x, y, argv[5], g);
            if (i < 0) { ED_OK("ARC error editor object add: outside the grid\n"); return; }
            for (int k = 6; k < argc; k++) { const char *eq = strchr(argv[k], '='); if (!eq) continue; char key[64]; size_t kl = (size_t)(eq - argv[k]); if (kl >= sizeof key) kl = sizeof key - 1; memcpy(key, argv[k], kl); key[kl] = 0; ed_obj_set(i, key, eq + 1, g); }
            E.s.osel = i; ed_validate(); ED_OK("ARC ok editor object add %d\n", i); return;
        }
        int i = argc > 3 ? atoi(argv[3]) : -1;
        if (strcmp(op, "list") && (i < 0 || i >= ed_nobj())) { ED_OK("ARC error editor object %s: no object %s (there are %d)\n", op, argc > 3 ? argv[3] : "?", ed_nobj()); return; }
        if (!strcmp(op, "set") && argc >= 5) { bool r = arc_editor_object_set(i, argv[4], argc >= 6 ? argv[5] : NULL); ED_OK("ARC ok editor object set %d %d\n", i, r ? 1 : 0); return; }
        if (!strcmp(op, "move") && argc >= 6) { bool r = arc_editor_object_move(i, atoi(argv[4]), atoi(argv[5])); ED_OK("ARC ok editor object move %d %d\n", i, r ? 1 : 0); return; }
        if (!strcmp(op, "del")) { arc_editor_object_delete(i); ED_OK("ARC ok editor object del %d\n", i); return; }
        if (!strcmp(op, "select")) { E.s.osel = i; E.s.tab = ED_TAB_OBJECTS; ED_OK("ARC ok editor object select %d\n", i); return; }
        if (!strcmp(op, "list")) { for (int k = 0; k < ed_nobj(); k++) { char line[512]; ed_oline(k, line); printf("ARC editor object %d %s\n", k, line); } ED_OK("ARC ok editor object list %d\n", ed_nobj()); return; }
        ED_OK("ARC error editor object: add X Y KIND [name=value ...] | set N NAME [VALUE] | move N X Y | del N | select N | list\n"); return;
    }
    if (!strcmp(sub, "setting") && argc >= 3) {   /* (0.10.9: the value is every word after the key) */
        if (!LV()) { ED_OK("ARC error editor setting: no level\n"); return; }
        char v[256] = ""; size_t n = 0;
        for (int k = 3; k < argc; k++) { int m = snprintf(v + n, sizeof v - n, "%s%s", k > 3 ? " " : "", argv[k]); if (m < 0 || (size_t)m >= sizeof v - n) break; n += (size_t)m; }
        bool r = arc_editor_set_setting(argv[2], argc >= 4 ? v : NULL); ED_OK("ARC ok editor setting %s %d\n", argv[2], r ? 1 : 0); return;
    }
    if (!strcmp(sub, "rename") && argc >= 3) {   /* 0.10.9: every word after it ("editor rename Cargo Hold") */
        if (!LV()) { ED_OK("ARC error editor rename: no level\n"); return; }
        char name[256] = ""; size_t n = 0;
        for (int k = 2; k < argc; k++) { int m = snprintf(name + n, sizeof name - n, "%s%s", k > 2 ? " " : "", argv[k]); if (m < 0 || (size_t)m >= sizeof name - n) break; n += (size_t)m; }
        bool r = arc_editor_set_layer_name(name); ED_OK("ARC ok editor rename %d\n", r ? 1 : 0); return;
    }
    if (!strcmp(sub, "select") && argc >= 3) {
        if (!strcmp(argv[2], "none")) { arc_editor_select(-1, -1, -1, -1); ED_OK("ARC ok editor select none\n"); return; }
        if (argc < 6) { ED_OK("ARC error editor select X0 Y0 X1 Y1 | none\n"); return; }
        arc_editor_select(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), atoi(argv[5])); ED_OK("ARC ok editor select %d %d %d %d\n", E.s.s0x, E.s.s0y, E.s.s1x, E.s.s1y); return;
    }
    if (!strcmp(sub, "copy") || !strcmp(sub, "cut")) { bool r = arc_editor_copy(!strcmp(sub, "cut")); if (r) ED_OK("ARC ok editor %s %dx%d\n", sub, E.cw, E.ch); else ED_OK("ARC error editor %s: select a rectangle first\n", sub); return; }
    if (!strcmp(sub, "paste") && argc >= 4) { bool r = arc_editor_paste(atoi(argv[2]), atoi(argv[3])); if (r) ED_OK("ARC ok editor paste %dx%d\n", E.cw, E.ch); else ED_OK("ARC error editor paste: the clipboard is empty\n"); return; }
    if (!strcmp(sub, "clear")) { bool r = arc_editor_clear_selection(); ED_OK("ARC ok editor clear %d\n", r ? 1 : 0); return; }
    if (!strcmp(sub, "rotate")) { arc_editor_rotate_clipboard(); ED_OK("ARC ok editor rotate %dx%d\n", E.cw, E.ch); return; }
    if (!strcmp(sub, "flip")) { arc_editor_flip_clipboard(argc >= 3 && !strcmp(argv[2], "v")); ED_OK("ARC ok editor flip\n"); return; }
    if (!strcmp(sub, "onion") && argc >= 3) { arc_editor_onion(!strcmp(argv[2], "above") ? -1 : !strcmp(argv[2], "below") ? 1 : 0); ED_OK("ARC ok editor onion %d\n", ed_onion_layer()); return; }
    if (!strcmp(sub, "playtest") && argc >= 4) { bool r = arc_editor_playtest(atoi(argv[2]), atoi(argv[3])); if (!r) ED_OK("ARC error editor playtest: the game gave no playtest hook\n"); return; }
    if (!strcmp(sub, "tab") && argc >= 3) {
        for (int t = 0; t < ED_TABS; t++) if (!strcmp(argv[2], ed_tab_names[t]) || (argv[2][0] == "tol"[t] && !argv[2][1])) { if (!ed_tab_ok(t)) { ED_OK("ARC error editor tab %s: not available\n", argv[2]); return; } E.s.tab = t; ED_OK("ARC ok editor tab %s\n", ed_tab_names[t]); return; }
        { char lc[16]; size_t k = 0; for (; argv[2][k] && k < 15; k++) lc[k] = (char)tolower((unsigned char)argv[2][k]); lc[k] = 0;
          for (int t = 0; t < ED_TABS; t++) { char tn[16]; size_t q = 0; for (; ed_tab_names[t][q]; q++) tn[q] = (char)tolower((unsigned char)ed_tab_names[t][q]); tn[q] = 0; if (!strcmp(lc, tn) && ed_tab_ok(t)) { E.s.tab = t; ED_OK("ARC ok editor tab %s\n", ed_tab_names[t]); return; } } }
        ED_OK("ARC error editor tab: tiles | objects | layer\n"); return;
    }
    if (!strcmp(sub, "kind") && argc >= 3) { int k = ed_kind_index(argv[2]); if (k < 0) { ED_OK("ARC error editor kind: no object kind %s\n", argv[2]); return; } E.s.okind = k; ED_OK("ARC ok editor kind %s\n", argv[2]); return; }
    if (!strcmp(sub, "dump")) {
        static char row[8192];
        for (int y = 0; y < E.d.height; y++) {
            size_t k = 0;
            for (int x = 0; x < E.d.width && k + 16 < sizeof row; x++) {
                int v = ed_get_raw(x, y);
                if (E.csize == 1) row[k++] = (v >= 32 && v < 127) ? (char)v : '?';
                else k += (size_t)snprintf(row + k, sizeof row - k, x ? " %d" : "%d", v);
            }
            row[k] = 0; printf("ARC editor row %02d %s\n", y, row);
        }
        ED_OK("ARC ok editor dump %dx%d\n", E.d.width, E.d.height); return;
    }
    if (!strcmp(sub, "info")) {
        ED_OK("ARC editor open=%d playing=%d tool=%s tile=%d undo=%d redo=%d dirty=%d hover=%d,%d message=\"%s\"\n", E.s.open, E.s.playing, ed_tool_names[E.s.tool],
              E.tiles[arc_clampi(E.s.sel, 0, E.d.tile_count - 1)].id, arc_editor_undo_steps(), ed_redo_steps(), E.s.dirty, E.s.hover_ok ? E.s.hx : -1, E.s.hover_ok ? E.s.hy : -1, E.msg);
        return;
    }
    ED_OK("ARC error editor: on|off | paint X Y CH | fill X0 Y0 X1 Y1 CH | line X0 Y0 X1 Y1 CH | flood X Y CH | undo | redo | save | load | dump | tile CH | tool NAME | play | pause | info | layer N | view 2d|3d|game | orbit YAW PITCH DIST [X Z] | object ... | setting KEY [VALUE] | rename NAME | select X0 Y0 X1 Y1|none | copy | cut | paste X Y | clear | rotate | flip h|v | onion above|below|off | playtest X Y | tab NAME | kind KIND\n");
#undef ED_OK
}
