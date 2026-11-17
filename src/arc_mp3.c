/* ARC MP3 decoder: MPEG-1, MPEG-2 and MPEG-2.5 Audio Layer III, written from the standard (ISO/IEC 11172-3 and
 * 13818-3). Mono and stereo, joint stereo (mid/side and intensity), long, short and mixed blocks, the bit
 * reservoir, CBR and VBR. Skips ID3v2 tags; reads the Xing/Info + LAME tag for gapless length (encoder delay and
 * padding), giving the same samples as FFmpeg. Free-format bitrates too. Not supported: Layers I and II.
 *
 * Decoding a frame: header -> side info -> main data (from the bit reservoir) -> per granule and channel:
 * scalefactors, Huffman-coded spectrum, requantise; joint stereo; reorder short blocks; alias reduction;
 * IMDCT with overlap-add; frequency inversion; 32-band polyphase synthesis. Floating point throughout.
 * The standard's tables are in arc_mp3_tables.h (tools/mp3_tables_gen.py). */
#include "arc_internal.h"
#include "arc_mp3_tables.h"

/* ---- tables computed once ---- */
static float mp3_pow43[8207];          /* |x|^(4/3) for Huffman values up to 15 + 2^13 - 1 */
static float mp3_cos36[36][18];        /* long IMDCT */
static float mp3_cos12[12][6];         /* short IMDCT */
static float mp3_win[4][36];           /* IMDCT windows by block type */
static float mp3_cs[8], mp3_ca[8];     /* alias reduction butterflies */
static float mp3_synth_n[64][32];      /* polyphase matrixing */
static float mp3_d[512];               /* synthesis window */
static float mp3_is1[7][2];            /* MPEG-1 intensity stereo (left, right) for is_pos 0..6 */
static bool mp3_ready;

static void mp3_init_tables(void) {
    if (mp3_ready) return;
    const double PI = 3.14159265358979323846;
    for (int i = 0; i < 8207; i++) mp3_pow43[i] = (float)pow((double)i, 4.0 / 3.0);
    for (int i = 0; i < 36; i++) for (int k = 0; k < 18; k++) mp3_cos36[i][k] = (float)cos(PI / 72.0 * (2 * i + 1 + 18) * (2 * k + 1));
    for (int i = 0; i < 12; i++) for (int k = 0; k < 6; k++) mp3_cos12[i][k] = (float)cos(PI / 24.0 * (2 * i + 1 + 6) * (2 * k + 1));
    for (int i = 0; i < 36; i++) {
        mp3_win[0][i] = (float)sin(PI / 36.0 * (i + 0.5));
        mp3_win[1][i] = i < 18 ? mp3_win[0][i] : i < 24 ? 1.0f : i < 30 ? (float)sin(PI / 12.0 * (i - 18 + 0.5)) : 0.0f;
        mp3_win[3][i] = i < 6 ? 0.0f : i < 12 ? (float)sin(PI / 12.0 * (i - 6 + 0.5)) : i < 18 ? 1.0f : mp3_win[0][i];
        mp3_win[2][i] = i < 12 ? (float)sin(PI / 12.0 * (i + 0.5)) : 0.0f;   /* one short window (12 taps) */
    }
    static const double c[8] = { -0.6, -0.535, -0.33, -0.185, -0.095, -0.041, -0.0142, -0.0037 };
    for (int i = 0; i < 8; i++) { double s = sqrt(1.0 + c[i] * c[i]); mp3_cs[i] = (float)(1.0 / s); mp3_ca[i] = (float)(c[i] / s); }
    for (int i = 0; i < 64; i++) for (int k = 0; k < 32; k++) mp3_synth_n[i][k] = (float)cos((16 + i) * (2 * k + 1) * PI / 64.0);
    for (int i = 0; i <= 256; i++) mp3_d[i] = (float)(mp3_window_half[i] / 65536.0);
    for (int i = 1; i < 256; i++) mp3_d[512 - i] = (i & 63) ? -mp3_d[i] : mp3_d[i];
    for (int p = 0; p < 7; p++) {
        double t = tan(p * PI / 12.0);
        mp3_is1[p][0] = p == 6 ? 1.0f : (float)(t / (1 + t)); mp3_is1[p][1] = p == 6 ? 0.0f : (float)(1 / (1 + t));
    }
    mp3_ready = true;
}

/* ---- headers ---- */
typedef struct {
    int version;        /* 1 = MPEG-1, 2 = MPEG-2, 3 = MPEG-2.5 */
    int lsf;            /* MPEG-2 or 2.5: one granule, LSF scalefactors */
    int rate, sri;      /* sample rate and its row in the band tables */
    int channels, mode, mode_ext, crc, size, side_size;
} mp3_header;

static bool mp3_parse_header(const uint8_t *p, mp3_header *h, int free_base) {   /* free_base: free-format frame size without padding */
    if (p[0] != 0xFF || (p[1] & 0xE0) != 0xE0) return false;
    int ver = (p[1] >> 3) & 3, layer = (p[1] >> 1) & 3, br = p[2] >> 4, sr = (p[2] >> 2) & 3, pad = (p[2] >> 1) & 1;
    if (ver == 1 || layer != 1 || br == 15 || sr == 3 || (br == 0 && free_base < 0)) return false;   /* reserved, not Layer III */
    static const int br1[15] = { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320 };
    static const int br2[15] = { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160 };
    static const int rates[3] = { 44100, 48000, 32000 };
    h->version = ver == 3 ? 1 : ver == 2 ? 2 : 3;
    h->lsf = h->version != 1;
    h->rate = rates[sr] >> (h->version - 1);
    h->sri = sr + 3 * (h->version - 1);
    h->mode = p[3] >> 6; h->mode_ext = (p[3] >> 4) & 3;
    h->channels = h->mode == 3 ? 1 : 2;
    h->crc = !(p[1] & 1);
    int kbps = h->lsf ? br2[br] : br1[br];
    h->size = br ? (h->lsf ? 72 : 144) * kbps * 1000 / h->rate + pad : free_base + pad;   /* free_base 0: size not known yet */
    h->side_size = h->lsf ? (h->channels == 1 ? 9 : 17) : (h->channels == 1 ? 17 : 32);
    return h->size > 4 + h->side_size || (br == 0 && free_base == 0);
}

/* ---- bit reader over the main data ---- */
typedef struct { const uint8_t *p; int len; int pos; } mp3_bits;   /* pos in bits */
static uint32_t mp3_get(mp3_bits *b, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++, b->pos++) {
        int byte = b->pos >> 3;
        v = (v << 1) | (byte < b->len ? (b->p[byte] >> (7 - (b->pos & 7))) & 1u : 0u);
    }
    return v;
}
static uint32_t mp3_peek32(const mp3_bits *b) {   /* the next 32 bits, left-aligned (zeros past the end) */
    int byte = b->pos >> 3, sh = b->pos & 7;
    uint64_t w = 0;
    for (int i = 0; i < 5; i++) w = (w << 8) | (uint64_t)(byte + i < b->len ? b->p[byte + i] : 0);
    return (uint32_t)(w >> (8 - sh));
}

/* ---- side info ---- */
typedef struct {
    int part23, big_values, global_gain, sfc, wsf, block_type, mixed, table[3], sbg[3];
    int r0_end, r1_end, preflag, sf_scale, count1_table;
} mp3_granule;
typedef struct { int main_data_begin; int scfsi[2][4]; mp3_granule gr[2][2]; } mp3_side;

static void mp3_read_side(mp3_bits *b, const mp3_header *h, mp3_side *s) {
    int nch = h->channels, ngr = h->lsf ? 1 : 2;
    s->main_data_begin = (int)mp3_get(b, h->lsf ? 8 : 9);
    mp3_get(b, h->lsf ? (nch == 1 ? 1 : 2) : (nch == 1 ? 5 : 3));
    if (!h->lsf) for (int ch = 0; ch < nch; ch++) for (int i = 0; i < 4; i++) s->scfsi[ch][i] = (int)mp3_get(b, 1);
    int band[23]; band[0] = 0;
    for (int i = 0; i < 22; i++) band[i + 1] = band[i] + mp3_band_long[h->sri][i];
    for (int gr = 0; gr < ngr; gr++)
        for (int ch = 0; ch < nch; ch++) {
            mp3_granule *g = &s->gr[gr][ch];
            g->part23 = (int)mp3_get(b, 12);
            g->big_values = (int)mp3_get(b, 9); if (g->big_values > 288) g->big_values = 288;
            g->global_gain = (int)mp3_get(b, 8);
            g->sfc = (int)mp3_get(b, h->lsf ? 9 : 4);
            g->wsf = (int)mp3_get(b, 1);
            if (g->wsf) {
                g->block_type = (int)mp3_get(b, 2); g->mixed = (int)mp3_get(b, 1);
                for (int i = 0; i < 2; i++) g->table[i] = (int)mp3_get(b, 5);
                g->table[2] = 0;
                for (int i = 0; i < 3; i++) g->sbg[i] = (int)mp3_get(b, 3);
                if (g->block_type == 0) g->block_type = 1;   /* invalid: a window switch needs a non-normal block */
                g->r0_end = g->block_type == 2 ? (h->sri != 8 ? 36 : 72) : h->sri <= 2 ? 36 : h->sri != 8 ? 54 : 108;   /* 8 kHz bands are twice as wide */
                g->r1_end = 576;
            } else {
                g->block_type = 0; g->mixed = 0;
                for (int i = 0; i < 3; i++) g->table[i] = (int)mp3_get(b, 5);
                g->sbg[0] = g->sbg[1] = g->sbg[2] = 0;
                int r0 = (int)mp3_get(b, 4), r1 = (int)mp3_get(b, 3);
                int a = r0 + 1, c = r0 + r1 + 2; if (a > 22) a = 22; if (c > 22) c = 22;
                g->r0_end = band[a]; g->r1_end = band[c];
            }
            int bv = g->big_values * 2;
            if (g->r0_end > bv) g->r0_end = bv;
            if (g->r1_end > bv) g->r1_end = bv;
            if (g->r1_end < g->r0_end) g->r1_end = g->r0_end;
            g->preflag = h->lsf ? 0 : (int)mp3_get(b, 1);
            g->sf_scale = (int)mp3_get(b, 1);
            g->count1_table = (int)mp3_get(b, 1);
        }
}

/* ---- decoder state ---- */
typedef struct {
    int long_end, short_start;            /* scalefactor bands coded long, and the first short band (13 = none) */
    int sf[40];                           /* long bands, then short band x window */
    int sf_bad[40];                       /* intensity: the position that means "not intensity" for this factor */
} mp3_sfs;

struct arc__mp3 {
    const uint8_t *data; size_t size, first, pos;
    int channels, rate, spf;
    int64_t skip, total, produced;        /* gapless trimming, per channel */
    uint8_t res[4096]; int res_len;       /* bit reservoir: main data of earlier frames */
    float overlap[2][32][18];
    float v[2][1024]; int voff[2];
    int prev_sf[2][22];                   /* MPEG-1 scfsi: granule 0's long scalefactors */
    int frame_channels;                   /* channels of the frame just decoded (a stream may switch) */
    int free_base;                        /* free-format bitrate: frame size without padding (-1: not free format) */
    bool native;                          /* tests: output each frame in its own channel count */
};

/* ---- scalefactors ---- */
static void mp3_scalefactors_mpeg1(mp3_bits *b, mp3_granule *g, int gr, int ch, const mp3_side *side, int prev_sf[22], mp3_sfs *o) {
    int s1 = mp3_slen[0][g->sfc], s2 = mp3_slen[1][g->sfc];
    memset(o->sf, 0, sizeof o->sf);
    for (int i = 0; i < 40; i++) o->sf_bad[i] = 7;
    if (g->block_type == 2) {
        int k = 0;
        if (g->mixed) { o->long_end = 8; o->short_start = 3; for (int i = 0; i < 8; i++) o->sf[k++] = (int)mp3_get(b, s1); }
        else { o->long_end = 0; o->short_start = 0; }
        for (int i = o->short_start; i < 12; i++) for (int w = 0; w < 3; w++) o->sf[k++] = (int)mp3_get(b, i < 6 ? s1 : s2);
    } else {
        o->long_end = 22; o->short_start = 13;
        static const int groups[5] = { 0, 6, 11, 16, 21 };
        for (int gi = 0; gi < 4; gi++)
            for (int i = groups[gi]; i < groups[gi + 1]; i++)
                o->sf[i] = (gr == 1 && side->scfsi[ch][gi]) ? prev_sf[i] : (int)mp3_get(b, gi < 2 ? s1 : s2);
        for (int i = 0; i < 22; i++) prev_sf[i] = o->sf[i];
    }
}
static void mp3_scalefactors_lsf(mp3_bits *b, mp3_granule *g, int intensity_right, mp3_sfs *o) {
    int slen[4], t, sfc = g->sfc;
    g->preflag = 0;
    if (intensity_right) {
        sfc >>= 1;
        if (sfc < 180) { slen[0] = sfc / 36; slen[1] = (sfc % 36) / 6; slen[2] = sfc % 6; slen[3] = 0; t = 3; }
        else if (sfc < 244) { sfc -= 180; slen[0] = (sfc % 64) >> 4; slen[1] = (sfc % 16) >> 2; slen[2] = sfc % 4; slen[3] = 0; t = 4; }
        else { sfc -= 244; slen[0] = sfc / 3; slen[1] = sfc % 3; slen[2] = slen[3] = 0; t = 5; }
    } else {
        if (sfc < 400) { slen[0] = (sfc >> 4) / 5; slen[1] = (sfc >> 4) % 5; slen[2] = (sfc & 15) >> 2; slen[3] = sfc & 3; t = 0; }
        else if (sfc < 500) { sfc -= 400; slen[0] = (sfc >> 2) / 5; slen[1] = (sfc >> 2) % 5; slen[2] = sfc & 3; slen[3] = 0; t = 1; }
        else { sfc -= 500; slen[0] = sfc / 3; slen[1] = sfc % 3; slen[2] = slen[3] = 0; t = 2; g->preflag = 1; }
    }
    int kind = g->block_type == 2 ? (g->mixed ? 2 : 1) : 0;
    if (g->block_type == 2) { o->long_end = g->mixed ? 6 : 0; o->short_start = g->mixed ? 3 : 0; }
    else { o->long_end = 22; o->short_start = 13; }
    memset(o->sf, 0, sizeof o->sf);
    for (int i = 0; i < 40; i++) o->sf_bad[i] = 1 << 30;
    int k = 0;
    for (int i = 0; i < 4; i++)
        for (int n = 0; n < mp3_lsf_nsf[t][kind][i] && k < 40; n++, k++) {
            o->sf[k] = (int)mp3_get(b, slen[i]);
            o->sf_bad[k] = (1 << slen[i]) - 1;
        }
}

/* ---- Huffman spectrum: fills is[576] (signed integers); returns the end of the non-zero part ---- */
static int mp3_huffman(mp3_bits *b, const mp3_granule *g, int end_bit, int *is) {
    int i = 0;
    for (int region = 0; region < 3; region++) {
        int stop = region == 0 ? g->r0_end : region == 1 ? g->r1_end : g->big_values * 2;
        int ts = g->table[region], tab = mp3_table_of[ts], lin = mp3_linbits[ts];
        for (; i < stop; i += 2) {
            if (tab < 0) { is[i] = is[i + 1] = 0; continue; }
            uint32_t w = mp3_peek32(b);
            int lo = mp3_huff_start[tab], hi = mp3_huff_start[tab + 1] - 1;   /* the largest code <= w */
            while (lo < hi) { int mid = (lo + hi + 1) >> 1; if (mp3_huff[mid].code <= w) lo = mid; else hi = mid - 1; }
            b->pos += mp3_huff[lo].len;
            int x = mp3_huff[lo].x, y = mp3_huff[lo].y;
            if (x == 15 && lin) x += (int)mp3_get(b, lin);
            if (x && mp3_get(b, 1)) x = -x;
            if (y == 15 && lin) y += (int)mp3_get(b, lin);
            if (y && mp3_get(b, 1)) y = -y;
            is[i] = x; is[i + 1] = y;
        }
    }
    /* count1: quadruples of -1, 0, 1 until the granule's bits run out */
    int qt = g->count1_table;
    while (i + 4 <= 576 && b->pos < end_bit) {
        int v = -1;
        if (qt) v = (int)mp3_get(b, 4) ^ 15;
        else {
            uint32_t w = mp3_peek32(b);
            for (int k = 0; k < 16; k++) {
                int L = mp3_quad_len[0][k];
                if ((w >> (32 - L)) == mp3_quad_code[0][k]) { v = k; b->pos += L; break; }
            }
            if (v < 0) break;
        }
        int q[4] = { (v >> 3) & 1, (v >> 2) & 1, (v >> 1) & 1, v & 1 };
        for (int k = 0; k < 4; k++) if (q[k] && mp3_get(b, 1)) q[k] = -1;
        if (b->pos > end_bit) break;                       /* ran past the granule: that quadruple is not real */
        for (int k = 0; k < 4; k++) is[i + k] = q[k];
        i += 4;
    }
    int nz = i;
    for (; i < 576; i++) is[i] = 0;
    return nz;
}

/* ---- requantise: xr = sign * |is|^(4/3) * 2^(exponent / 4) ---- */
static float mp3_gain(int e) { static const float q[4] = { 1.0f, 1.18920712f, 1.41421356f, 1.68179283f }; return ldexpf(q[e & 3], e >> 2); }
static void mp3_requantise(const mp3_granule *g, const mp3_sfs *s, int sri, const int *is, float *xr) {
    int gain = g->global_gain - 210, shift = g->sf_scale + 1, i = 0, k;
    for (k = 0; k < s->long_end && i < 576; k++) {
        float f = mp3_gain(gain - ((s->sf[k] + (g->preflag ? mp3_pretab[k] : 0)) << shift));
        for (int n = 0; n < mp3_band_long[sri][k] && i < 576; n++, i++) {
            int v = is[i]; int a = v < 0 ? -v : v; if (a > 8206) a = 8206;
            xr[i] = v ? (v < 0 ? -mp3_pow43[a] : mp3_pow43[a]) * f : 0.0f;
        }
    }
    int sk = s->long_end;
    for (int band = s->short_start; band < 13 && i < 576; band++)
        for (int w = 0; w < 3; w++) {
            float f = mp3_gain(gain - (g->sbg[w] << 3) - (s->sf[sk++] << shift));
            for (int n = 0; n < mp3_band_short[sri][band] && i < 576; n++, i++) {
                int v = is[i]; int a = v < 0 ? -v : v; if (a > 8206) a = 8206;
                xr[i] = v ? (v < 0 ? -mp3_pow43[a] : mp3_pow43[a]) * f : 0.0f;
            }
        }
    for (; i < 576; i++) xr[i] = 0;
}

/* ---- joint stereo (on the spectrum in bitstream order, before short blocks are reordered) ---- */
static void mp3_ms(float *l, float *r, int n) {
    const float s = 0.70710678118654752f;
    for (int j = 0; j < n; j++) { float a = l[j], b = r[j]; l[j] = (a + b) * s; r[j] = (a - b) * s; }
}
static void mp3_is_apply(float *l, float *r, int n, int pos, const mp3_header *h, int intensity_scale) {
    float kl, kr;
    if (!h->lsf) { kl = mp3_is1[pos][0]; kr = mp3_is1[pos][1]; }
    else {
        float base = intensity_scale ? 0.70710678f : 0.84089642f;   /* 2^-1/2 or 2^-1/4 */
        if (pos == 0) kl = kr = 1;
        else if (pos & 1) { kl = powf(base, (float)((pos + 1) / 2)); kr = 1; }
        else { kl = 1; kr = powf(base, (float)(pos / 2)); }
    }
    for (int j = 0; j < n; j++) { float a = l[j]; l[j] = a * kl; r[j] = a * kr; }
}
static void mp3_stereo(const mp3_header *h, const mp3_granule *gr, const mp3_sfs *sr, float *l, float *r) {
    bool ms = (h->mode_ext & 2) != 0;
    if (!(h->mode_ext & 1)) { if (ms) mp3_ms(l, r, 576); return; }
    const uint8_t *bl = mp3_band_long[h->sri], *bs = mp3_band_short[h->sri];
    int scale = gr->sfc & 1;
    float *t0 = l + 576, *t1 = r + 576;
    bool found = false;
    if (gr->block_type == 2) {
        bool found_w[3] = { false, false, false };
        int k = (13 - sr->short_start) * 3 + sr->long_end - 3;
        for (int i = 12; i >= sr->short_start; i--) {
            if (i != 11) k -= 3;
            int len = bs[i];
            for (int w = 2; w >= 0; w--) {
                t0 -= len; t1 -= len;
                bool is_band = false;
                if (!found_w[w]) {
                    for (int j = 0; j < len; j++) if (t1[j] != 0) { found_w[w] = true; break; }
                    int pos = sr->sf[k + w], legal = k + w >= 0 && pos != sr->sf_bad[k + w] && (h->lsf || pos < 7);
                    if (i == 12) {   /* the last band: band 11's position only if band 11 is intensity too (see long) */
                        int len11 = bs[11]; bool zero11 = true;
                        const float *b11 = t1 - w * len - 3 * len11 + w * len11;   /* band 11, same window (bitstream order) */
                        for (int j = 0; j < len11; j++) if (b11[j] != 0) { zero11 = false; break; }
                        if (!zero11) { pos = 0; legal = h->lsf; }
                    }
                    if (!found_w[w] && legal) { mp3_is_apply(t0, t1, len, pos, h, scale); is_band = true; }
                }
                if (!is_band && ms) mp3_ms(t0, t1, len);
            }
        }
        found = found_w[0] || found_w[1] || found_w[2];
    }
    for (int i = sr->long_end - 1; i >= 0; i--) {
        int len = bl[i];
        t0 -= len; t1 -= len;
        bool is_band = false;
        if (!found) {
            for (int j = 0; j < len; j++) if (t1[j] != 0) { found = true; break; }
            int pos, legal;
            if (i == 21) {   /* no scalefactor of its own: band 20's position if band 20 is intensity too, else the
                                standard's initial value (MPEG-2: position 0; MPEG-1: not intensity) */
                int len20 = bl[20]; bool zero20 = true;
                for (int j = 0; j < len20; j++) if (t1[j - len20] != 0) { zero20 = false; break; }
                bool legal20 = sr->sf[20] != sr->sf_bad[20] && (h->lsf || sr->sf[20] < 7);
                if (zero20) { pos = sr->sf[20]; legal = legal20; } else { pos = 0; legal = h->lsf; }
            } else { pos = sr->sf[i]; legal = pos != sr->sf_bad[i] && (h->lsf || pos < 7); }
            if (!found && legal) { mp3_is_apply(t0, t1, len, pos, h, scale); is_band = true; }
        }
        if (!is_band && ms) mp3_ms(t0, t1, len);
    }
}

/* ---- reorder short blocks: (band, window, frequency) -> (band, frequency, window) ---- */
static void mp3_reorder(const mp3_granule *g, const mp3_sfs *s, int sri, float *xr) {
    if (g->block_type != 2) return;
    float tmp[576];
    float *p = xr + (g->mixed ? (sri != 8 ? 36 : 72) : 0);
    for (int band = s->short_start; band < 13; band++) {
        int len = mp3_band_short[sri][band];
        if (p + 3 * len > xr + 576) break;
        for (int f = 0; f < len; f++) for (int w = 0; w < 3; w++) tmp[f * 3 + w] = p[w * len + f];
        memcpy(p, tmp, sizeof(float) * (size_t)len * 3);
        p += 3 * len;
    }
}

/* ---- alias reduction between subbands ---- */
static void mp3_antialias(const mp3_granule *g, float *xr) {
    int n = g->block_type == 2 ? (g->mixed ? 1 : 0) : 31;
    for (int sb = 1; sb <= n; sb++) {
        float *p = xr + sb * 18;
        for (int i = 0; i < 8; i++) {
            float bu = p[-1 - i], bd = p[i];
            p[-1 - i] = bu * mp3_cs[i] - bd * mp3_ca[i];
            p[i] = bd * mp3_cs[i] + bu * mp3_ca[i];
        }
    }
}

/* ---- IMDCT + overlap-add + frequency inversion: xr[576] -> out[576] (subband-major, 18 per band) ---- */
static void mp3_imdct(const mp3_granule *g, float *xr, float overlap[32][18], float *out) {
    for (int sb = 0; sb < 32; sb++) {
        const float *x = xr + sb * 18;
        float z[36];
        int bt = (g->mixed && sb < 2) ? 0 : g->block_type;
        if (bt != 2) {
            for (int i = 0; i < 36; i++) {
                float s = 0;
                for (int k = 0; k < 18; k++) s += x[k] * mp3_cos36[i][k];
                z[i] = s * mp3_win[bt][i];
            }
        } else {
            memset(z, 0, sizeof z);
            for (int w = 0; w < 3; w++)
                for (int i = 0; i < 12; i++) {
                    float s = 0;
                    for (int k = 0; k < 6; k++) s += x[3 * k + w] * mp3_cos12[i][k];
                    z[6 + 6 * w + i] += s * mp3_win[2][i];
                }
        }
        float *o = out + sb * 18;
        for (int i = 0; i < 18; i++) { o[i] = z[i] + overlap[sb][i]; overlap[sb][i] = z[i + 18]; }
        if (sb & 1) for (int i = 1; i < 18; i += 2) o[i] = -o[i];
    }
}

/* ---- polyphase synthesis: 18 slots of 32 subband samples -> 576 PCM samples ---- */
static void mp3_synth(struct arc__mp3 *m, int ch, const float *hy, float *pcm) {
    float *v = m->v[ch];
    for (int t = 0; t < 18; t++) {
        int off = m->voff[ch] = (m->voff[ch] - 64) & 1023;
        float s[32];
        for (int k = 0; k < 32; k++) s[k] = hy[k * 18 + t];
        for (int i = 0; i < 64; i++) {
            float a = 0; const float *n = mp3_synth_n[i];
            for (int k = 0; k < 32; k++) a += n[k] * s[k];
            v[(off + i) & 1023] = a;
        }
        for (int j = 0; j < 32; j++) {
            float a = 0;
            for (int i = 0; i < 8; i++) {
                a += mp3_d[64 * i + j] * v[(off + 128 * i + j) & 1023];
                a += mp3_d[64 * i + 32 + j] * v[(off + 128 * i + 96 + j) & 1023];
            }
            pcm[t * 32 + j] = a;
        }
    }
}

/* ---- opening: ID3v2, the first frame, Xing/Info + LAME ---- */
static size_t mp3_find_frame(const uint8_t *d, size_t n, size_t at, mp3_header *h, int *free_base) {
    for (; at + 4 <= n; at++) {
        if (!mp3_parse_header(d + at, h, *free_base)) continue;
        if (h->size <= 4 + h->side_size) {   /* free format, size unknown: it runs to the next matching header */
            size_t nx = at + 4 + (size_t)h->side_size;
            while (nx + 4 <= n && !(d[nx] == d[at] && d[nx + 1] == d[at + 1] && (d[nx + 2] & 0xFD) == (d[at + 2] & 0xFD) && (d[nx + 3] & 0xC0) == (d[at + 3] & 0xC0))) nx++;
            if (nx + 4 > n) continue;
            int pad = (d[at + 2] >> 1) & 1;
            *free_base = (int)(nx - at) - pad;
            mp3_parse_header(d + at, h, *free_base);
        }
        mp3_header h2;   /* a real frame is followed by another (or the end) */
        size_t next = at + (size_t)h->size;
        if (next + 4 <= n && !(mp3_parse_header(d + next, &h2, *free_base) && h2.rate == h->rate)) continue;
        return at;
    }
    return (size_t)-1;
}
static uint32_t rd32be(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

size_t arc__mp3_state_size(void) { return sizeof(struct arc__mp3); }

void arc__mp3_rewind(arc__mp3 *m) {
    m->pos = m->first; m->produced = 0; m->res_len = 0;
    memset(m->overlap, 0, sizeof m->overlap); memset(m->v, 0, sizeof m->v); m->voff[0] = m->voff[1] = 0;
    memset(m->prev_sf, 0, sizeof m->prev_sf);
}

bool arc__mp3_open(arc__mp3 *m, const uint8_t *d, size_t n) {
    mp3_init_tables();
    memset(m, 0, sizeof *m);
    size_t at = 0;
    if (n >= 10 && !memcmp(d, "ID3", 3)) {
        size_t sz = ((size_t)(d[6] & 127) << 21) | ((size_t)(d[7] & 127) << 14) | ((size_t)(d[8] & 127) << 7) | (d[9] & 127);
        at = 10 + sz + ((d[5] & 0x10) ? 10 : 0);
    }
    mp3_header h;
    m->free_base = 0;
    at = mp3_find_frame(d, n, at, &h, &m->free_base);
    if (at == (size_t)-1) return false;
    if (d[at + 2] >> 4) m->free_base = -1;   /* a fixed bitrate: no free-format frames later */
    m->data = d; m->size = n; m->channels = h.channels; m->rate = h.rate; m->spf = h.lsf ? 576 : 1152;
    m->skip = 0; m->total = -1;
    /* a Xing/Info frame carries no audio: read the gapless numbers and start after it */
    size_t x = at + 4 + (h.crc ? 2 : 0) + (size_t)h.side_size;
    if (x + 8 <= n && (!memcmp(d + x, "Xing", 4) || !memcmp(d + x, "Info", 4))) {
        uint32_t flags = rd32be(d + x + 4); size_t q = x + 8; int64_t frames = -1;
        if (flags & 1) { frames = rd32be(d + q); q += 4; }
        if (flags & 2) q += 4;
        if (flags & 4) q += 100;
        if (flags & 8) q += 4;
        if (q + 24 <= n && (!memcmp(d + q, "LAME", 4) || !memcmp(d + q, "Lavf", 4) || !memcmp(d + q, "Lavc", 4))) {
            int delay = (d[q + 21] << 4) | (d[q + 22] >> 4), padding = ((d[q + 22] & 15) << 8) | d[q + 23];
            m->skip = delay + 529;              /* the encoder's delay plus the decoder's own 529 samples */
            if (frames > 0) m->total = frames * m->spf - delay - padding;
        }
        at += (size_t)h.size;
    } else if (at + 36 + 4 <= n && !memcmp(d + at + 36, "VBRI", 4)) at += (size_t)h.size;
    m->first = at;
    arc__mp3_rewind(m);
    return true;
}
int arc__mp3_channels(const arc__mp3 *m) { return m->channels; }
int arc__mp3_rate(const arc__mp3 *m) { return m->rate; }
int64_t arc__mp3_length(const arc__mp3 *m) { return m->total; }   /* samples per channel, -1 if the file does not say */

/* decode one frame into out (interleaved, up to 1152 x channels); returns samples per channel, -1 at the end */
static int mp3_decode_frame(arc__mp3 *m, float *pcm) {
    mp3_header h; size_t at = m->pos;
    for (;;) {
        if (at + 4 > m->size) return -1;
        if (mp3_parse_header(m->data + at, &h, m->free_base) && h.rate == m->rate) break;
        at = mp3_find_frame(m->data, m->size, at + 1, &h, &m->free_base);   /* lost sync, or a tag at the end */
        if (at == (size_t)-1 || h.rate != m->rate) return -1;
    }
    if (at + (size_t)h.size > m->size) return -1;
    m->pos = at + (size_t)h.size;
    const uint8_t *f = m->data + at;
    size_t side_at = 4 + (h.crc ? 2 : 0);
    mp3_bits sb = { f + side_at, h.side_size, 0 };
    mp3_side side; mp3_read_side(&sb, &h, &side);
    int main_len = h.size - (int)side_at - h.side_size;
    const uint8_t *main_data = f + side_at + h.side_size;
    int spf = h.lsf ? 576 : 1152, nch = h.channels, ngr = h.lsf ? 1 : 2;
    m->frame_channels = nch;
    /* the frame's main data starts main_data_begin bytes back in the reservoir */
    uint8_t buf[4096 + 1500];
    bool ok = side.main_data_begin <= m->res_len;
    int blen = 0;
    if (ok) {
        memcpy(buf, m->res + m->res_len - side.main_data_begin, (size_t)side.main_data_begin); blen = side.main_data_begin;
        memcpy(buf + blen, main_data, (size_t)main_len); blen += main_len;
    }
    /* keep the newest bytes for later frames */
    if (main_len >= (int)sizeof m->res) { memcpy(m->res, main_data + main_len - (int)sizeof m->res, sizeof m->res); m->res_len = (int)sizeof m->res; }
    else {
        int keep = m->res_len + main_len > (int)sizeof m->res ? (int)sizeof m->res - main_len : m->res_len;
        memmove(m->res, m->res + m->res_len - keep, (size_t)keep);
        memcpy(m->res + keep, main_data, (size_t)main_len); m->res_len = keep + main_len;
    }
    if (!ok) { memset(pcm, 0, sizeof(float) * (size_t)spf * nch); return spf; }   /* not enough reservoir yet: silence */

    mp3_bits b = { buf, blen, 0 };
    int is[576]; float xr[2][576], hy[576], out[576];
    for (int gr = 0; gr < ngr; gr++) {
        mp3_sfs sfs[2]; int nz[2];
        for (int ch = 0; ch < nch; ch++) {
            mp3_granule *g = &side.gr[gr][ch];
            int start = b.pos, end = start + g->part23;
            if (!h.lsf) mp3_scalefactors_mpeg1(&b, g, gr, ch, &side, m->prev_sf[ch], &sfs[ch]);
            else mp3_scalefactors_lsf(&b, g, ch == 1 && (h.mode_ext & 1), &sfs[ch]);
            if (b.pos > end) { memset(xr[ch], 0, sizeof xr[ch]); nz[ch] = 0; b.pos = end; continue; }
            nz[ch] = mp3_huffman(&b, g, end, is);
            mp3_requantise(g, &sfs[ch], h.sri, is, xr[ch]);
            b.pos = end;
        }
        (void)nz;
        if (nch == 2 && h.mode == 1) mp3_stereo(&h, &side.gr[gr][1], &sfs[1], xr[0], xr[1]);
        for (int ch = 0; ch < nch; ch++) {
            mp3_granule *g = &side.gr[gr][ch];
            mp3_reorder(g, &sfs[ch], h.sri, xr[ch]);
            mp3_antialias(g, xr[ch]);
            mp3_imdct(g, xr[ch], m->overlap[ch], hy);
            mp3_synth(m, ch, hy, out);
            for (int i = 0; i < 576; i++) pcm[(gr * 576 + i) * nch + ch] = out[i];
        }
    }
    return spf;
}

/* the next frame's samples after gapless trimming, as 16-bit; returns samples per channel (0 is possible), -1 at the end */
int arc__mp3_read(arc__mp3 *m, int16_t *out) {
    for (;;) {
        if (m->total >= 0 && m->produced >= m->skip + m->total) return -1;
        float pcm[1152 * 2];
        int n = mp3_decode_frame(m, pcm);
        if (n < 0) return -1;
        int64_t a = m->produced, b = m->produced + n;
        m->produced = b;
        int64_t lo = a > m->skip ? a : m->skip, hi = b;
        if (m->total >= 0 && hi > m->skip + m->total) hi = m->skip + m->total;
        if (hi <= lo) continue;
        int nch = m->native ? m->frame_channels : m->channels, fch = m->frame_channels, c = 0;
        for (int64_t s = lo; s < hi; s++, c++)
            for (int ch = 0; ch < nch; ch++) {
                const float *f = pcm + (s - a) * fch;   /* a frame with other channels than the stream began with: mix */
                float v = (fch == nch ? f[ch] : fch == 2 ? (f[0] + f[1]) * 0.5f : f[0]) * 32768.0f;
                v = v < -32768.0f ? -32768.0f : v > 32767.0f ? 32767.0f : v;
                out[c * nch + ch] = (int16_t)lrintf(v);
            }
        return c;
    }
}
void arc__mp3_set_native_channels(arc__mp3 *m, bool on) { m->native = on; }
int  arc__mp3_frame_channels(const arc__mp3 *m) { return m->frame_channels; }
