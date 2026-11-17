/* ARC JPEG decoder: baseline and extended sequential (Huffman) JPEG, as written by cameras, Pillow, Blender and
 * glTF exporters. Greyscale or YCbCr, any chroma subsampling, restart markers. Progressive and arithmetic-coded
 * files are refused (tools/mesh2arcm.py re-encodes those as baseline). Written from ITU-T T.81. */
#include "arc_internal.h"

typedef struct { uint8_t bits[17]; uint8_t vals[256]; uint16_t lookup[512]; int maxcode[18], valptr[17], mincode[17]; } jpg_huff;
typedef struct { int id, h, v, tq, td, ta, bw, bh, pred; uint8_t *pix; int stride; } jpg_comp;
typedef struct {
    const uint8_t *d; size_t n, pos;
    uint64_t bitbuf; int bitcnt; bool marker_hit;   /* 0.8.3: 64-bit, refilled once a coefficient */
    uint16_t q[4][64]; jpg_huff hdc[4], hac[4];
    jpg_comp c[3]; int nc, w, h, hmax, vmax, mcux, mcuy, restart;
    int coef[64]; uint8_t set[64]; int nset;     /* 0.8.3: kept zero between blocks; set[] = the slots a block wrote */
    int sh, bs;                                   /* 0.8.3: decoded at 1 / 2^sh: each block gives bs x bs (8 >> sh) */
    bool adobe_rgb;
} jpg;

static const uint8_t jpg_zigzag[64] = { 0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
                                     35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 };

static void jpg_huff_build(jpg_huff *h) {
    int code = 0, k = 0;
    for (int i = 0; i < 512; i++) h->lookup[i] = 0;
    for (int len = 1; len <= 16; len++) {
        h->valptr[len] = k; h->mincode[len] = code;
        for (int i = 0; i < h->bits[len]; i++, k++, code++)
            if (len <= 9) {   /* fast table: 9-bit prefix -> length << 8 | value */
                int shift = 9 - len;
                for (int f = 0; f < (1 << shift); f++) h->lookup[(code << shift) | f] = (uint16_t)(len << 8 | h->vals[k]);
            }
        h->maxcode[len] = h->bits[len] ? code - 1 : -1;
        code <<= 1;
    }
    h->maxcode[17] = 0x7FFFFFFF;
}
static void jpg_fill_bits(jpg *j) {
    while (j->bitcnt <= 56) {
        uint32_t b = 0;
        if (!j->marker_hit && j->pos < j->n) {
            b = j->d[j->pos++];
            if (b == 0xFF) {
                uint8_t nx = j->pos < j->n ? j->d[j->pos] : 0;
                if (nx == 0) j->pos++;                       /* stuffed byte */
                else { j->marker_hit = true; j->pos--; b = 0; }   /* a marker: feed zeros until it is handled */
            }
        }
        j->bitbuf |= (uint64_t)b << (56 - j->bitcnt); j->bitcnt += 8;
    }
}
/* the _nf versions assume at least 32 bits are buffered (one jpg_fill_bits covers a code and its value) */
static inline int jpg_get_bits_nf(jpg *j, int n) {
    if (!n) return 0;
    int v = (int)(j->bitbuf >> (64 - n)); j->bitbuf <<= n; j->bitcnt -= n; return v;
}
static inline int jpg_decode_huff_nf(jpg *j, const jpg_huff *h) {
    uint16_t e = h->lookup[j->bitbuf >> 55];
    if (e) { int len = e >> 8; j->bitbuf <<= len; j->bitcnt -= len; return e & 0xFF; }
    int code = 0, len = 9;
    for (; len <= 16; len++) {
        code = (int)(j->bitbuf >> (64 - len));
        if (code <= h->maxcode[len]) break;
    }
    if (len > 16) return -1;
    j->bitbuf <<= len; j->bitcnt -= len;
    int idx = h->valptr[len] + code - h->mincode[len];
    return idx >= 0 && idx < 256 ? h->vals[idx] : -1;
}
static int jpg_extend(int v, int n) { return n && v < (1 << (n - 1)) ? v - (1 << n) + 1 : v; }

/* 0.8.3: the accurate integer IDCT (the "islow" method of the IJG libjpeg, 13-bit constants), with shortcuts for
   columns and rows that hold only their DC term. Within 1 of an exact float IDCT. */
#define JI_C(x) ((int32_t)((x) * 8192.0 + 0.5))
/* clamping by table, as libjpeg's range limit: JCLAMP(v) for v in -384..639 gives 0..255; anything further out
   (only from damaged data) wraps harmlessly */
static uint8_t jpg_clamp_t[1024];
#define JCLAMP(v) jpg_clamp_t[(v) & 1023]
#define JI_DESCALE(x, n) (((x) + ((int32_t)1 << ((n) - 1))) >> (n))
static void jpg_idct_block(const int *in, uint8_t *out, int stride) {
    int32_t ws[64];
    for (int c = 0; c < 8; c++) {           /* columns, results scaled by 4 (PASS1_BITS 2) */
        const int *p = in + c; int32_t *w = ws + c;
        if (!(p[8] | p[16] | p[24] | p[32] | p[40] | p[48] | p[56])) {
            int32_t dc = (int32_t)p[0] * 4;
            w[0] = w[8] = w[16] = w[24] = w[32] = w[40] = w[48] = w[56] = dc; continue;
        }
        int32_t z2 = p[16], z3 = p[48], z1 = (z2 + z3) * JI_C(0.541196100);
        int32_t t2 = z1 + z3 * -JI_C(1.847759065), t3 = z1 + z2 * JI_C(0.765366865);
        int32_t t0 = ((int32_t)p[0] + p[32]) * 8192, t1 = ((int32_t)p[0] - p[32]) * 8192;
        int32_t t10 = t0 + t3, t13 = t0 - t3, t11 = t1 + t2, t12 = t1 - t2;
        int32_t o0 = p[56], o1 = p[40], o2 = p[24], o3 = p[8];
        int32_t y1 = o0 + o3, y2 = o1 + o2, y3 = o0 + o2, y4 = o1 + o3, y5 = (y3 + y4) * JI_C(1.175875602);
        o0 *= JI_C(0.298631336); o1 *= JI_C(2.053119869); o2 *= JI_C(3.072711026); o3 *= JI_C(1.501321110);
        y1 *= -JI_C(0.899976223); y2 *= -JI_C(2.562915447); y3 *= -JI_C(1.961570560); y4 *= -JI_C(0.390180644);
        y3 += y5; y4 += y5;
        o0 += y1 + y3; o1 += y2 + y4; o2 += y2 + y3; o3 += y1 + y4;
        w[0] = JI_DESCALE(t10 + o3, 11); w[56] = JI_DESCALE(t10 - o3, 11);
        w[8] = JI_DESCALE(t11 + o2, 11); w[48] = JI_DESCALE(t11 - o2, 11);
        w[16] = JI_DESCALE(t12 + o1, 11); w[40] = JI_DESCALE(t12 - o1, 11);
        w[24] = JI_DESCALE(t13 + o0, 11); w[32] = JI_DESCALE(t13 - o0, 11);
    }
    for (int r = 0; r < 8; r++) {           /* rows: back to pixels, +128 */
        const int32_t *w = ws + r * 8; uint8_t *o = out + r * stride;
        if (!(w[1] | w[2] | w[3] | w[4] | w[5] | w[6] | w[7])) {
            uint8_t b = JCLAMP(JI_DESCALE(w[0], 5) + 128);
            o[0] = o[1] = o[2] = o[3] = o[4] = o[5] = o[6] = o[7] = b; continue;
        }
        int32_t z2 = w[2], z3 = w[6], z1 = (z2 + z3) * JI_C(0.541196100);
        int32_t t2 = z1 + z3 * -JI_C(1.847759065), t3 = z1 + z2 * JI_C(0.765366865);
        int32_t t0 = (w[0] + w[4]) * 8192, t1 = (w[0] - w[4]) * 8192;
        int32_t t10 = t0 + t3, t13 = t0 - t3, t11 = t1 + t2, t12 = t1 - t2;
        int32_t o0 = w[7], o1 = w[5], o2 = w[3], o3 = w[1];
        int32_t y1 = o0 + o3, y2 = o1 + o2, y3 = o0 + o2, y4 = o1 + o3, y5 = (y3 + y4) * JI_C(1.175875602);
        o0 *= JI_C(0.298631336); o1 *= JI_C(2.053119869); o2 *= JI_C(3.072711026); o3 *= JI_C(1.501321110);
        y1 *= -JI_C(0.899976223); y2 *= -JI_C(2.562915447); y3 *= -JI_C(1.961570560); y4 *= -JI_C(0.390180644);
        y3 += y5; y4 += y5;
        o0 += y1 + y3; o1 += y2 + y4; o2 += y2 + y3; o3 += y1 + y4;
        int v[8] = { JI_DESCALE(t10 + o3, 18), JI_DESCALE(t11 + o2, 18), JI_DESCALE(t12 + o1, 18), JI_DESCALE(t13 + o0, 18),
                     JI_DESCALE(t13 - o0, 18), JI_DESCALE(t12 - o1, 18), JI_DESCALE(t11 - o2, 18), JI_DESCALE(t10 - o3, 18) };
        for (int k = 0; k < 8; k++) o[k] = JCLAMP(v[k] + 128);
    }
}
/* colour tables (as libjpeg's): Cr -> R, Cb -> B, and the two G terms, rounded once */
static int jpg_cr_r[256], jpg_cb_b[256], jpg_cr_g[256], jpg_cb_g[256];
static void jpg_idct_init(void) {
    static volatile int done; if (done) return;
    for (int i = 0; i < 1024; i++) jpg_clamp_t[i] = (uint8_t)(i < 256 ? i : i < 640 ? 255 : 0);
    for (int i = 0; i < 256; i++) {
        int x = i - 128;
        jpg_cr_r[i] = (int)floor(1.402 * x + 0.5);
        jpg_cb_b[i] = (int)floor(1.772 * x + 0.5);
        jpg_cr_g[i] = (int)lrint(-0.714136 * 65536 * x); jpg_cb_g[i] = (int)lrint(-0.344136 * 65536 * x) + 32768;
    }
    done = 1;
}
static bool jpg_decode_block(jpg *j, jpg_comp *c, uint8_t *out, int stride) {
    int *coef = j->coef; bool ac = false; j->nset = 0;
    if (j->bitcnt < 32) jpg_fill_bits(j);
    int t = jpg_decode_huff_nf(j, &j->hdc[c->td]); if (t < 0 || t > 16) return false;
    int diff = jpg_extend(jpg_get_bits_nf(j, t), t); c->pred += diff;
    const uint16_t *q = j->q[c->tq];
    coef[0] = c->pred * q[0];
    for (int k = 1; k < 64;) {
        if (j->bitcnt < 32) jpg_fill_bits(j);
        int rs = jpg_decode_huff_nf(j, &j->hac[c->ta]); if (rs < 0) return false;
        int r = rs >> 4, s = rs & 15;
        if (!s) { if (r == 15) { k += 16; continue; } break; }
        k += r; if (k > 63) return false;
        int z = jpg_zigzag[k]; coef[z] = jpg_extend(jpg_get_bits_nf(j, s), s) * q[k]; j->set[j->nset++] = (uint8_t)z; k++; ac = true;
    }
    if (!ac || j->sh == 3) {   /* only the DC term (or 1/8 size, which is the block's mean = DC): flat */
        uint8_t b = JCLAMP(JI_DESCALE(coef[0], 3) + 128);
        for (int y = 0; y < j->bs; y++) memset(out + (size_t)y * stride, b, (size_t)j->bs);
        coef[0] = 0; for (int i = 0; i < j->nset; i++) coef[j->set[i]] = 0;
        return true;
    }
    if (j->sh) {   /* 1/2 or 1/4 size: the block's pixels averaged in 2 x 2 or 4 x 4 */
        uint8_t t[64]; jpg_idct_block(coef, t, 8);
        int n = 1 << j->sh, area = n * n;
        for (int y = 0; y < j->bs; y++)
            for (int x = 0; x < j->bs; x++) {
                int sum = 0;
                for (int v = 0; v < n; v++) for (int u = 0; u < n; u++) sum += t[(y * n + v) * 8 + x * n + u];
                out[(size_t)y * stride + x] = (uint8_t)((sum + area / 2) / area);
            }
        coef[0] = 0; for (int i = 0; i < j->nset; i++) coef[j->set[i]] = 0;
        return true;
    }
    jpg_idct_block(coef, out, stride);
    coef[0] = 0; for (int i = 0; i < j->nset; i++) coef[j->set[i]] = 0;
    return true;
}
static uint16_t jpg_be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

static bool jpg_scan(jpg *j, const uint8_t *sos, int len) {
    int ns = sos[0]; if (ns < 1 || ns > 3 || len < 1 + ns * 2 + 3) return false;
    jpg_comp *sc[3];
    for (int i = 0; i < ns; i++) {
        int id = sos[1 + i * 2], tables = sos[2 + i * 2]; sc[i] = NULL;
        for (int k = 0; k < j->nc; k++) if (j->c[k].id == id) sc[i] = &j->c[k];
        if (!sc[i]) return false;
        sc[i]->td = tables >> 4; sc[i]->ta = tables & 15;
        if (sc[i]->td > 3 || sc[i]->ta > 3) return false;
    }
    j->bitbuf = 0; j->bitcnt = 0; j->marker_hit = false;
    for (int i = 0; i < j->nc; i++) j->c[i].pred = 0;
    int mcux, mcuy, todo = j->restart;
    if (ns == 1) {   /* non-interleaved: one block per MCU, over the component's own block grid */
        jpg_comp *c = sc[0];
        mcux = (j->w * c->h / j->hmax + 7) / 8; mcuy = (j->h * c->v / j->vmax + 7) / 8;
        for (int by = 0; by < mcuy; by++)
            for (int bx = 0; bx < mcux; bx++) {
                if (!jpg_decode_block(j, c, c->pix + (size_t)by * j->bs * c->stride + bx * j->bs, c->stride)) return false;
                if (j->restart && --todo == 0 && !(by == mcuy - 1 && bx == mcux - 1)) {
                    todo = j->restart;
                    if (j->pos + 1 < j->n && j->d[j->pos] == 0xFF && (j->d[j->pos + 1] & 0xF8) == 0xD0) j->pos += 2;
                    j->bitbuf = 0; j->bitcnt = 0; j->marker_hit = false; c->pred = 0;
                }
            }
        return true;
    }
    for (int my = 0; my < j->mcuy; my++)
        for (int mx = 0; mx < j->mcux; mx++) {
            for (int i = 0; i < ns; i++) {
                jpg_comp *c = sc[i];
                for (int yy = 0; yy < c->v; yy++)
                    for (int xx = 0; xx < c->h; xx++) {
                        int bx = mx * c->h + xx, by = my * c->v + yy;
                        if (!jpg_decode_block(j, c, c->pix + (size_t)by * j->bs * c->stride + bx * j->bs, c->stride)) return false;
                    }
            }
            if (j->restart && --todo == 0 && !(my == j->mcuy - 1 && mx == j->mcux - 1)) {
                todo = j->restart;
                if (j->pos + 1 < j->n && j->d[j->pos] == 0xFF && (j->d[j->pos + 1] & 0xF8) == 0xD0) j->pos += 2;
                j->bitbuf = 0; j->bitcnt = 0; j->marker_hit = false;
                for (int i = 0; i < j->nc; i++) j->c[i].pred = 0;
            }
        }
    return true;
}

/* ---- 0.8.3 colour stage: each output row's samples (x16, integers), then fixed-point YCbCr -> RGB ----
   Chroma is upsampled bilinearly at sample centres, as before. For 2x (and 1x) subsampling the weights are exactly
   1/4 and 3/4, so 4:2:0, 4:2:2, 4:4:0 and 4:4:4 run in integers; other ratios keep the float path. */
static void jpg_row(const jpg *j, const jpg_comp *c, int y, int32_t *o, int32_t *tmp) {
    int rh = j->hmax / c->h, rv = j->vmax / c->v;
    int cw = (j->w * c->h + j->hmax - 1) / j->hmax, ch = (j->h * c->v + j->vmax - 1) / j->vmax;
    bool fast = (j->hmax % c->h == 0) && (j->vmax % c->v == 0) && rh <= 2 && rv <= 2;
    if (!fast) {   /* any other ratio: the general bilinear path */
        for (int x = 0; x < j->w; x++) {
            float fx = ((float)x + 0.5f) * (float)c->h / (float)j->hmax - 0.5f, fy = ((float)y + 0.5f) * (float)c->v / (float)j->vmax - 0.5f;
            int x0 = (int)floorf(fx), y0 = (int)floorf(fy); float ax = fx - (float)x0, ay = fy - (float)y0;
            int xa = arc_clampi(x0, 0, cw - 1), xb = arc_clampi(x0 + 1, 0, cw - 1), ya = arc_clampi(y0, 0, ch - 1), yb = arc_clampi(y0 + 1, 0, ch - 1);
            const uint8_t *P = c->pix;
            float v = (P[(size_t)ya * c->stride + xa] * (1 - ax) + P[(size_t)ya * c->stride + xb] * ax) * (1 - ay) +
                      (P[(size_t)yb * c->stride + xa] * (1 - ax) + P[(size_t)yb * c->stride + xb] * ax) * ay;
            o[x] = (int32_t)(v * 16.0f + 0.5f);
        }
        return;
    }
    /* vertical: one row (x4) or two rows weighted 1:3 / 3:1 */
    const uint8_t *ra, *rb; int wa, wb;
    if (rv == 1) { ra = rb = c->pix + (size_t)y * c->stride; wa = 4; wb = 0; }
    else {
        int y0 = (y & 1) ? (y - 1) / 2 : y / 2 - 1, ya = arc_clampi(y0, 0, ch - 1), yb = arc_clampi(y0 + 1, 0, ch - 1);
        ra = c->pix + (size_t)ya * c->stride; rb = c->pix + (size_t)yb * c->stride;
        wa = (y & 1) ? 3 : 1; wb = 4 - wa;
    }
    if (rh == 1) { for (int x = 0; x < j->w; x++) o[x] = (ra[x] * wa + rb[x] * wb) * 4; return; }
    for (int x = 0; x < cw; x++) tmp[x] = ra[x] * wa + rb[x] * wb;
    for (int x = 0; x < j->w; x++) {   /* horizontal 1:3 / 3:1 */
        int x0 = (x & 1) ? (x - 1) / 2 : x / 2 - 1, xa = x0 < 0 ? 0 : x0, xb = x0 + 1 > cw - 1 ? cw - 1 : x0 + 1;
        o[x] = (x & 1) ? tmp[xa] * 3 + tmp[xb] : tmp[xa] + tmp[xb] * 3;
    }
}
static bool jpg_to_rgba(const jpg *j, uint8_t *rgba) {
    bool full = true;
    for (int i = 0; i < j->nc; i++) if (j->c[i].h != j->hmax || j->c[i].v != j->vmax) full = false;
    if (full && j->nc == 3 && !j->adobe_rgb) {   /* 4:4:4 (and 1x1 sampling): straight from the planes, by table */
        for (int y = 0; y < j->h; y++) {
            const uint8_t *Y = j->c[0].pix + (size_t)y * j->c[0].stride, *B = j->c[1].pix + (size_t)y * j->c[1].stride, *R = j->c[2].pix + (size_t)y * j->c[2].stride;
            uint8_t *o = rgba + (size_t)y * j->w * 4;
            for (int x = 0; x < j->w; x++) {
                int yy = Y[x], cb = B[x], cr = R[x];
                o[0] = JCLAMP(yy + jpg_cr_r[cr]);
                o[1] = JCLAMP(yy + ((jpg_cb_g[cb] + jpg_cr_g[cr]) >> 16));
                o[2] = JCLAMP(yy + jpg_cb_b[cb]);
                o[3] = 255; o += 4;
            }
        }
        return true;
    }
    int32_t *buf = (int32_t *)malloc(sizeof(int32_t) * (size_t)j->w * 4 + 64); if (!buf) return false;
    int32_t *v0 = buf, *v1 = buf + j->w, *v2 = buf + 2 * j->w, *tmp = buf + 3 * j->w;
    for (int y = 0; y < j->h; y++) {
        uint8_t *o = rgba + (size_t)y * j->w * 4;
        jpg_row(j, &j->c[0], y, v0, tmp);
        if (j->nc == 1) {
            for (int x = 0; x < j->w; x++) { uint8_t g = (uint8_t)(v0[x] >> 4); o[x * 4] = o[x * 4 + 1] = o[x * 4 + 2] = g; o[x * 4 + 3] = 255; }
            continue;
        }
        jpg_row(j, &j->c[1], y, v1, tmp); jpg_row(j, &j->c[2], y, v2, tmp);
        if (j->adobe_rgb) {
            for (int x = 0; x < j->w; x++) {
                int r = (v0[x] + 8) >> 4, g = (v1[x] + 8) >> 4, b = (v2[x] + 8) >> 4;
                o[x * 4] = (uint8_t)(r > 255 ? 255 : r); o[x * 4 + 1] = (uint8_t)(g > 255 ? 255 : g); o[x * 4 + 2] = (uint8_t)(b > 255 ? 255 : b); o[x * 4 + 3] = 255;
            }
            continue;
        }
        for (int x = 0; x < j->w; x++) {   /* x16 samples: 4096 a unit, then >> 16 with rounding */
            int32_t Y = v0[x] * 4096 + 32768, cb = v1[x] - 2048, cr = v2[x] - 2048;
            int32_t r = (Y + 5743 * cr) >> 16, g = (Y - 1410 * cb - 2925 * cr) >> 16, b = (Y + 7258 * cb) >> 16;
            o[x * 4] = (uint8_t)(r < 0 ? 0 : r > 255 ? 255 : r); o[x * 4 + 1] = (uint8_t)(g < 0 ? 0 : g > 255 ? 255 : g);
            o[x * 4 + 2] = (uint8_t)(b < 0 ? 0 : b > 255 ? 255 : b); o[x * 4 + 3] = 255;
        }
    }
    free(buf);
    return true;
}

/* decode to RGBA (malloc'd); NULL if the data is not a JPEG this decoder handles */
/* 0.8.3: max_side > 0 decodes at 1/2, 1/4 or 1/8 size: the largest reduction (up to 1/8) whose largest side is
   still at least max_side, sides rounded up as halving rounds them. The texture cap then halves whatever is left,
   so the result has the same size as decoding in full and halving. */
uint8_t *arc__jpeg_read_max(const uint8_t *d, size_t n, int *w_out, int *h_out, int max_side);
uint8_t *arc__jpeg_read(const uint8_t *d, size_t n, int *w_out, int *h_out) { return arc__jpeg_read_max(d, n, w_out, h_out, 0); }
uint8_t *arc__jpeg_read_max(const uint8_t *d, size_t n, int *w_out, int *h_out, int max_side) {
    if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) return NULL;
    jpg_idct_init();
    jpg *j = (jpg *)calloc(1, sizeof(jpg)); if (!j) return NULL;
    j->d = d; j->n = n; j->pos = 2; j->bs = 8;
    uint8_t *rgba = NULL; bool frame = false, ok = false;
    while (j->pos + 4 <= n) {
        if (d[j->pos] != 0xFF) { j->pos++; continue; }
        uint8_t m = d[j->pos + 1]; j->pos += 2;
        if (m == 0xD8 || (m >= 0xD0 && m <= 0xD7) || m == 0x01 || m == 0xFF) { if (m == 0xFF) j->pos--; continue; }
        if (m == 0xD9) break;
        if (j->pos + 2 > n) break;
        int len = jpg_be16(d + j->pos) - 2; const uint8_t *p = d + j->pos + 2;
        if (len < 0 || j->pos + 2 + (size_t)len > n) break;
        if (m == 0xDB) {                                     /* quantisation tables */
            for (int at = 0; at < len;) {
                int pq = p[at] >> 4, tq = p[at] & 15; at++;
                if (tq > 3 || at + (pq ? 128 : 64) > len) goto fail;
                for (int k = 0; k < 64; k++) j->q[tq][k] = pq ? jpg_be16(p + at + k * 2) : p[at + k];
                at += pq ? 128 : 64;
            }
        } else if (m == 0xC4) {                              /* Huffman tables */
            for (int at = 0; at < len;) {
                int tc = p[at] >> 4, th = p[at] & 15; at++;
                if (th > 3 || tc > 1 || at + 16 > len) goto fail;
                jpg_huff *h = tc ? &j->hac[th] : &j->hdc[th]; int total = 0;
                h->bits[0] = 0; for (int i = 1; i <= 16; i++) { h->bits[i] = p[at + i - 1]; total += h->bits[i]; }
                at += 16; if (total > 256 || at + total > len) goto fail;
                memcpy(h->vals, p + at, (size_t)total); at += total;
                jpg_huff_build(h);
            }
        } else if (m == 0xDD) { if (len >= 2) j->restart = jpg_be16(p); }
        else if (m == 0xEE) { if (len >= 12 && !memcmp(p, "Adobe", 5)) j->adobe_rgb = p[11] == 0; }
        else if (m == 0xC0 || m == 0xC1) {                   /* baseline / extended sequential, Huffman */
            if (len < 6 || p[0] != 8) goto fail;
            j->h = jpg_be16(p + 1); j->w = jpg_be16(p + 3); j->nc = p[5];
            if (j->w <= 0 || j->h <= 0 || (j->nc != 1 && j->nc != 3) || len < 6 + j->nc * 3) goto fail;
            if ((size_t)j->w * j->h > (size_t)1 << 28) goto fail;
            if (max_side > 0) {   /* halve while the halved size is still at least max_side (never below it) */
                int big = j->w > j->h ? j->w : j->h;
                while (j->sh < 3 && ((big + (1 << (j->sh + 1)) - 1) >> (j->sh + 1)) >= max_side) j->sh++;
                j->bs = 8 >> j->sh;
            }
            j->hmax = j->vmax = 1;
            for (int i = 0; i < j->nc; i++) {
                jpg_comp *c = &j->c[i]; c->id = p[6 + i * 3]; c->h = p[7 + i * 3] >> 4; c->v = p[7 + i * 3] & 15; c->tq = p[8 + i * 3];
                if (c->h < 1 || c->h > 4 || c->v < 1 || c->v > 4 || c->tq > 3) goto fail;
                if (c->h > j->hmax) j->hmax = c->h;
                if (c->v > j->vmax) j->vmax = c->v;
            }
            j->mcux = (j->w + 8 * j->hmax - 1) / (8 * j->hmax); j->mcuy = (j->h + 8 * j->vmax - 1) / (8 * j->vmax);
            for (int i = 0; i < j->nc; i++) {
                jpg_comp *c = &j->c[i];
                c->bw = j->mcux * c->h * j->bs; c->bh = j->mcuy * c->v * j->bs; c->stride = c->bw;
                c->pix = (uint8_t *)calloc((size_t)c->bw * c->bh, 1); if (!c->pix) goto fail;
            }
            frame = true;
        } else if (m >= 0xC2 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
            arc_log("JPEG: progressive or arithmetic-coded images are not supported (re-save as baseline)"); goto fail;
        } else if (m == 0xDA) {                              /* start of scan */
            if (!frame) goto fail;
            j->pos += 2 + (size_t)len;
            if (!jpg_scan(j, p, len)) goto fail;
            ok = true;
            continue;
        }
        j->pos += 2 + (size_t)len;
    }
    if (!ok) goto fail;
    if (j->sh) { j->w = (j->w + (1 << j->sh) - 1) >> j->sh; j->h = (j->h + (1 << j->sh) - 1) >> j->sh; }   /* the reduced picture */
    rgba = (uint8_t *)malloc((size_t)j->w * j->h * 4); if (!rgba) goto fail;
    if (!jpg_to_rgba(j, rgba)) goto fail;
    *w_out = j->w; *h_out = j->h;
    for (int i = 0; i < 3; i++) free(j->c[i].pix);
    free(j);
    return rgba;
fail:
    for (int i = 0; i < 3; i++) free(j->c[i].pix);
    free(j); free(rgba);
    return NULL;
}

/* ======================================================================== */
/* Baseline JPEG writer (4:2:0, standard Huffman tables): screenshots small enough for itch.io (3 MB limit)    */
/* ======================================================================== */
static const uint8_t JW_ZIG[64] = { 0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20,
    13, 6, 7, 14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54,
    47, 55, 62, 63 };
static const uint8_t JW_QL[64] = { 16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56, 14, 17,
    22, 29, 51, 87, 80, 62, 18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92, 49, 64, 78, 87, 103, 121, 120, 101, 72,
    92, 95, 98, 112, 100, 103, 99 };
static const uint8_t JW_QC[64] = { 17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99, 99, 99, 47, 66,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99 };
/* Annex K tables: code counts per length 1..16, then the symbols */
static const uint8_t JW_DC_L_N[16] = { 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 }, JW_DC_L_V[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
static const uint8_t JW_DC_C_N[16] = { 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 }, JW_DC_C_V[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
static const uint8_t JW_AC_L_N[16] = { 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d };
static const uint8_t JW_AC_L_V[162] = { 0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
    0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09,
    0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45,
    0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73,
    0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
    0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
    0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6,
    0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa };
static const uint8_t JW_AC_C_N[16] = { 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77 };
static const uint8_t JW_AC_C_V[162] = { 0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
    0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16,
    0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44,
    0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a,
    0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96,
    0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba,
    0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe2, 0xe3, 0xe4, 0xe5,
    0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa };
typedef struct { uint16_t code[256]; uint8_t len[256]; } jw_huff;
typedef struct { FILE *f; uint32_t acc; int nbits; bool err; } jw_out;
static void jw_build(jw_huff *h, const uint8_t *n, const uint8_t *v) {
    memset(h, 0, sizeof *h); uint16_t code = 0; int k = 0;
    for (int l = 1; l <= 16; l++) { for (int i = 0; i < n[l - 1]; i++, k++) { h->code[v[k]] = code++; h->len[v[k]] = (uint8_t)l; } code <<= 1; }
}
static void jw_bits(jw_out *o, uint32_t bits, int n) {
    o->acc = (o->acc << n) | (bits & ((1u << n) - 1)); o->nbits += n;
    while (o->nbits >= 8) {
        uint8_t b = (uint8_t)(o->acc >> (o->nbits - 8)); o->nbits -= 8;
        if (fputc(b, o->f) == EOF) o->err = true;
        if (b == 0xFF && fputc(0, o->f) == EOF) o->err = true;   /* byte stuffing */
    }
}
static void jw_put(FILE *f, const void *p, size_t n) { fwrite(p, 1, n, f); }
static void jw_marker(FILE *f, uint8_t m, int len) { uint8_t h[4] = { 0xFF, m, (uint8_t)(len >> 8), (uint8_t)len }; jw_put(f, h, 4); }
static void jw_dct(float *d) {   /* separable 8x8 DCT-II, orthonormal scaling as JPEG expects */
    static float C[8][8]; static bool init;
    if (!init) { for (int u = 0; u < 8; u++) for (int x = 0; x < 8; x++) C[u][x] = (u ? 0.5f : 0.35355339f) * cosf((float)((2 * x + 1) * u) * 3.14159265f / 16); init = true; }
    float t[64];
    for (int y = 0; y < 8; y++) for (int u = 0; u < 8; u++) { float s = 0; for (int x = 0; x < 8; x++) s += C[u][x] * d[y * 8 + x]; t[y * 8 + u] = s; }
    for (int u = 0; u < 8; u++) for (int v = 0; v < 8; v++) { float s = 0; for (int y = 0; y < 8; y++) s += C[v][y] * t[y * 8 + u]; d[v * 8 + u] = s; }
}
static void jw_block(jw_out *o, float *d, const float *q, int *dc, const jw_huff *hdc, const jw_huff *hac) {
    jw_dct(d);
    int z[64];
    for (int i = 0; i < 64; i++) { float v = d[JW_ZIG[i]] / q[i]; z[i] = (int)(v < 0 ? v - 0.5f : v + 0.5f); }
    int diff = z[0] - *dc; *dc = z[0];
    int a = diff < 0 ? -diff : diff, nb = 0; while (a >> nb) nb++;
    jw_bits(o, hdc->code[nb], hdc->len[nb]); if (nb) jw_bits(o, (uint32_t)(diff < 0 ? diff - 1 : diff), nb);
    int run = 0;
    for (int i = 1; i < 64; i++) {
        if (!z[i]) { run++; continue; }
        while (run > 15) { jw_bits(o, hac->code[0xF0], hac->len[0xF0]); run -= 16; }
        a = z[i] < 0 ? -z[i] : z[i]; nb = 0; while (a >> nb) nb++;
        int sym = (run << 4) | nb; jw_bits(o, hac->code[sym], hac->len[sym]); jw_bits(o, (uint32_t)(z[i] < 0 ? z[i] - 1 : z[i]), nb);
        run = 0;
    }
    if (run) jw_bits(o, hac->code[0], hac->len[0]);   /* end of block */
}
bool arc_image_save_jpeg(const char *path, const uint8_t *rgba, int w, int h, int quality) {
    if (!path || !rgba || w <= 0 || h <= 0 || w > 65535 || h > 65535) return false;
    FILE *f = fopen(path, "wb"); if (!f) return false;
    quality = quality <= 0 ? 90 : quality > 100 ? 100 : quality;
    int sc = quality < 50 ? 5000 / quality : 200 - quality * 2;
    uint8_t ql[64], qc[64]; float fql[64], fqc[64];
    for (int i = 0; i < 64; i++) {
        int a = (JW_QL[JW_ZIG[i]] * sc + 50) / 100, b = (JW_QC[JW_ZIG[i]] * sc + 50) / 100;
        ql[i] = (uint8_t)(a < 1 ? 1 : a > 255 ? 255 : a); qc[i] = (uint8_t)(b < 1 ? 1 : b > 255 ? 255 : b);
        fql[i] = ql[i]; fqc[i] = qc[i];
    }
    static const uint8_t soi_app0[] = { 0xFF, 0xD8, 0xFF, 0xE0, 0, 16, 'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0 };
    jw_put(f, soi_app0, sizeof soi_app0);
    jw_marker(f, 0xDB, 2 + 2 * 65); fputc(0, f); jw_put(f, ql, 64); fputc(1, f); jw_put(f, qc, 64);
    uint8_t sof[15] = { 8, (uint8_t)(h >> 8), (uint8_t)h, (uint8_t)(w >> 8), (uint8_t)w, 3, 1, 0x22, 0, 2, 0x11, 1, 3, 0x11, 1 };
    jw_marker(f, 0xC0, 2 + 15); jw_put(f, sof, 15);
    const uint8_t *tn[4] = { JW_DC_L_N, JW_AC_L_N, JW_DC_C_N, JW_AC_C_N }, *tv[4] = { JW_DC_L_V, JW_AC_L_V, JW_DC_C_V, JW_AC_C_V };
    const int tid[4] = { 0x00, 0x10, 0x01, 0x11 }, tc[4] = { 12, 162, 12, 162 };
    for (int t = 0; t < 4; t++) { jw_marker(f, 0xC4, 2 + 1 + 16 + tc[t]); fputc(tid[t], f); jw_put(f, tn[t], 16); jw_put(f, tv[t], (size_t)tc[t]); }
    static const uint8_t sos[10] = { 3, 1, 0x00, 2, 0x11, 3, 0x11, 0, 63, 0 };
    jw_marker(f, 0xDA, 2 + 10); jw_put(f, sos, 10);
    jw_huff hdl, hal, hdc, hac; jw_build(&hdl, JW_DC_L_N, JW_DC_L_V); jw_build(&hal, JW_AC_L_N, JW_AC_L_V);
    jw_build(&hdc, JW_DC_C_N, JW_DC_C_V); jw_build(&hac, JW_AC_C_N, JW_AC_C_V);
    jw_out o = { f, 0, 0, false }; int dcy = 0, dcb = 0, dcr = 0;
    for (int my = 0; my < h; my += 16) for (int mx = 0; mx < w; mx += 16) {   /* one MCU: four 8x8 Y blocks, one Cb, one Cr */
        float Y[4][64], Cb[64] = { 0 }, Cr[64] = { 0 };
        for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
            int px = mx + x < w ? mx + x : w - 1, py = my + y < h ? my + y : h - 1;   /* repeat the edge past the picture */
            const uint8_t *p = rgba + ((size_t)py * w + px) * 4;
            float r = p[0], g = p[1], b = p[2];
            Y[(y >> 3) * 2 + (x >> 3)][(y & 7) * 8 + (x & 7)] = 0.299f * r + 0.587f * g + 0.114f * b - 128;
            int ci = (y >> 1) * 8 + (x >> 1);
            Cb[ci] += (-0.168736f * r - 0.331264f * g + 0.5f * b) * 0.25f;
            Cr[ci] += (0.5f * r - 0.418688f * g - 0.081312f * b) * 0.25f;
        }
        for (int k = 0; k < 4; k++) jw_block(&o, Y[k], fql, &dcy, &hdl, &hal);
        jw_block(&o, Cb, fqc, &dcb, &hdc, &hac); jw_block(&o, Cr, fqc, &dcr, &hdc, &hac);
    }
    if (o.nbits) jw_bits(&o, 0x7F, 8 - o.nbits);   /* pad with ones */
    fputc(0xFF, f); fputc(0xD9, f);
    bool ok = !o.err && !ferror(f); if (fclose(f)) ok = false;
    return ok;
}
static bool jw_ext(const char *e, const char *want) { for (; *e && *want; e++, want++) if ((*e | 32) != *want) return false; return !*e && !*want; }
/* PNG or JPEG by the file's extension (.jpg/.jpeg -> JPEG quality 90) */
bool arc__image_write(const char *path, const uint8_t *rgba, int w, int h) {
    const char *dot = path ? strrchr(path, '.') : NULL;
    if (dot && (jw_ext(dot, ".jpg") || jw_ext(dot, ".jpeg"))) return arc_image_save_jpeg(path, rgba, w, h, 90);
    return arc__png_write(path, rgba, w, h);
}
