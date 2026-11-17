/* ARC animated GIF writer (for agents recording motion): median-cut palette per frame, LZW, looping. */
#include "arc_internal.h"


static void gif_u16(FILE *f, int v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }
bool arc__gif_begin(arc__gif *g, const char *path, int w, int h, double delay_cs) {
    memset(g, 0, sizeof *g);
    g->f = fopen(path, "wb"); if (!g->f) return false;
    g->w = w; g->h = h; g->delay_cs = delay_cs;
    fwrite("GIF89a", 1, 6, g->f); gif_u16(g->f, w); gif_u16(g->f, h);
    fputc(0x70, g->f); fputc(0, g->f); fputc(0, g->f);                 /* no global colour table, 8-bit colour resolution */
    static const uint8_t loop[19] = { 0x21, 0xFF, 11, 'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0', 3, 1, 0, 0, 0 };
    fwrite(loop, 1, sizeof loop, g->f);
    return true;
}

/* ---- palette: median cut over a 5-5-5 histogram ---- */
typedef struct { uint8_t r, g, b; uint32_t n; uint16_t key; } gif_bin;
typedef struct { int first, count; } gif_box;
static int g_cut_ch;
static int bin_cmp(const void *a, const void *b) {
    const gif_bin *x = (const gif_bin *)a, *y = (const gif_bin *)b;
    int vx = g_cut_ch == 0 ? x->r : g_cut_ch == 1 ? x->g : x->b, vy = g_cut_ch == 0 ? y->r : g_cut_ch == 1 ? y->g : y->b;
    return vx != vy ? vx - vy : (int)x->key - (int)y->key;
}
static void build_palette(const uint8_t *rgba, int npx, uint8_t pal[256][3], int *pal_n, uint8_t *map /* 32768 */) {
    static uint32_t hist[32768]; memset(hist, 0, sizeof hist);
    for (int i = 0; i < npx; i++) { const uint8_t *p = rgba + (size_t)i * 4; hist[(p[0] >> 3) << 10 | (p[1] >> 3) << 5 | (p[2] >> 3)]++; }
    static gif_bin bins[32768]; int nb = 0;
    for (int k = 0; k < 32768; k++) if (hist[k]) { gif_bin *b = &bins[nb++]; b->r = (uint8_t)(k >> 10); b->g = (uint8_t)((k >> 5) & 31); b->b = (uint8_t)(k & 31); b->n = hist[k]; b->key = (uint16_t)k; }
    gif_box boxes[256]; int nbox = 1; boxes[0].first = 0; boxes[0].count = nb;
    while (nbox < 256) {
        int best = -1; double best_score = 0; int best_ch = 0;
        for (int i = 0; i < nbox; i++) {
            if (boxes[i].count < 2) continue;
            int lo[3] = { 31, 31, 31 }, hi[3] = { 0, 0, 0 }; double tot = 0;
            for (int j = 0; j < boxes[i].count; j++) {
                const gif_bin *b = &bins[boxes[i].first + j]; int v[3] = { b->r, b->g, b->b };
                for (int c = 0; c < 3; c++) { if (v[c] < lo[c]) lo[c] = v[c]; if (v[c] > hi[c]) hi[c] = v[c]; }
                tot += b->n;
            }
            int ch = 0; for (int c = 1; c < 3; c++) if (hi[c] - lo[c] > hi[ch] - lo[ch]) ch = c;
            double score = (double)(hi[ch] - lo[ch]) * sqrt(tot);
            if (hi[ch] > lo[ch] && score > best_score) { best_score = score; best = i; best_ch = ch; }
        }
        if (best < 0) break;
        gif_box *bx = &boxes[best];
        g_cut_ch = best_ch; qsort(bins + bx->first, (size_t)bx->count, sizeof(gif_bin), bin_cmp);
        double tot = 0; for (int j = 0; j < bx->count; j++) tot += bins[bx->first + j].n;
        double acc = 0; int split = 1;
        for (int j = 0; j < bx->count - 1; j++) { acc += bins[bx->first + j].n; if (acc >= tot * 0.5) { split = j + 1; break; } split = j + 1; }
        boxes[nbox].first = bx->first + split; boxes[nbox].count = bx->count - split; bx->count = split; nbox++;
    }
    for (int i = 0; i < nbox; i++) {
        double r = 0, g = 0, b = 0, n = 0;
        for (int j = 0; j < boxes[i].count; j++) { const gif_bin *q = &bins[boxes[i].first + j]; r += q->r * (double)q->n; g += q->g * (double)q->n; b += q->b * (double)q->n; n += q->n; map[q->key] = (uint8_t)i; }
        if (n <= 0) n = 1;
        pal[i][0] = (uint8_t)arc_clampi((int)(r / n * 255.0 / 31.0 + 0.5), 0, 255);
        pal[i][1] = (uint8_t)arc_clampi((int)(g / n * 255.0 / 31.0 + 0.5), 0, 255);
        pal[i][2] = (uint8_t)arc_clampi((int)(b / n * 255.0 / 31.0 + 0.5), 0, 255);
    }
    *pal_n = nbox ? nbox : 1;
    if (!nbox) pal[0][0] = pal[0][1] = pal[0][2] = 0;
}

/* ---- LZW ---- */
typedef struct { FILE *f; uint8_t block[255]; int nblock; uint32_t bits; int nbits; } gif_out;
static void out_byte(gif_out *o, uint8_t b) { o->block[o->nblock++] = b; if (o->nblock == 255) { fputc(255, o->f); fwrite(o->block, 1, 255, o->f); o->nblock = 0; } }
static void out_code(gif_out *o, int code, int size) { o->bits |= (uint32_t)code << o->nbits; o->nbits += size; while (o->nbits >= 8) { out_byte(o, (uint8_t)o->bits); o->bits >>= 8; o->nbits -= 8; } }
static void lzw(FILE *f, const uint8_t *idx, int n) {
    enum { MIN = 8, CLEAR = 256, EOI = 257, HSIZE = 8192 };
    static int32_t hkey[HSIZE]; static int16_t hval[HSIZE];
    gif_out o; memset(&o, 0, sizeof o); o.f = f;
    fputc(MIN, f);
    int size = MIN + 1, next = EOI + 1;
    memset(hkey, -1, sizeof hkey);
    out_code(&o, CLEAR, size);
    int prefix = idx[0];
    for (int i = 1; i < n; i++) {
        int c = idx[i]; int32_t key = (prefix << 8) | c;
        uint32_t h = ((uint32_t)key * 2654435761u) >> 19; int found = -1;
        while (hkey[h] != -1) { if (hkey[h] == key) { found = hval[h]; break; } h = (h + 1) & (HSIZE - 1); }
        if (found >= 0) { prefix = found; continue; }
        out_code(&o, prefix, size);
        if (next < 4096) {
            hkey[h] = key; hval[h] = (int16_t)next;
            if (next == (1 << size) && size < 12) size++;
            next++;
        } else {
            out_code(&o, CLEAR, size); size = MIN + 1; next = EOI + 1; memset(hkey, -1, sizeof hkey);
        }
        prefix = c;
    }
    out_code(&o, prefix, size);
    out_code(&o, EOI, size);
    if (o.nbits > 0) out_byte(&o, (uint8_t)o.bits);
    if (o.nblock) { fputc(o.nblock, f); fwrite(o.block, 1, (size_t)o.nblock, f); }
    fputc(0, f);
}
/* one frame, RGBA top row first, the size given to arc__gif_begin */
void arc__gif_frame(arc__gif *g, const uint8_t *rgba) {
    if (!g->f) return;
    int npx = g->w * g->h;
    uint8_t pal[256][3]; int pn = 0; static uint8_t map[32768];
    build_palette(rgba, npx, pal, &pn, map);
    uint8_t *idx = (uint8_t *)arc__xalloc((size_t)npx);
    for (int i = 0; i < npx; i++) { const uint8_t *p = rgba + (size_t)i * 4; idx[i] = map[(p[0] >> 3) << 10 | (p[1] >> 3) << 5 | (p[2] >> 3)]; }
    g->delay_acc += g->delay_cs; int d = (int)g->delay_acc; g->delay_acc -= d;
    uint8_t gce[8] = { 0x21, 0xF9, 4, 0x04, (uint8_t)(d & 255), (uint8_t)(d >> 8), 0, 0 };   /* dispose: leave in place */
    fwrite(gce, 1, 8, g->f);
    fputc(0x2C, g->f); gif_u16(g->f, 0); gif_u16(g->f, 0); gif_u16(g->f, g->w); gif_u16(g->f, g->h);
    fputc(0x87, g->f);                                                                 /* local colour table, 256 entries */
    for (int i = 0; i < 256; i++) { if (i < pn) fwrite(pal[i], 1, 3, g->f); else { fputc(0, g->f); fputc(0, g->f); fputc(0, g->f); } }
    lzw(g->f, idx, npx);
    free(idx);
}
bool arc__gif_end(arc__gif *g) {
    if (!g->f) return false;
    fputc(0x3B, g->f); bool ok = fclose(g->f) == 0; g->f = NULL; return ok;
}
