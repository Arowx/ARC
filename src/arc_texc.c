/* ARC block-compressed textures (0.7.1): BC1, BC3, BC4, BC5 and BC7 block decoders (the CPU fallback when a GPU
 * cannot take a format, and the software renderer) and the .arct container that tools/texcompress.py writes.
 *
 * .arct (little endian): "ARCT" u32 version (1), u32 format (ARC_TEXFMT_*), u32 width, u32 height, u32 levels,
 *   u32 flags (1 = a normal map: BC5 holds X, Y and Z is rebuilt; 2 = the payload is a zlib stream), u32 payload bytes,
 *   then the payload: every mip level's blocks, level 0 first, each level ((w + 3) / 4) * ((h + 3) / 4) blocks of
 *   8 (BC1, BC4) or 16 bytes (BC3, BC5, BC7), rows of blocks top to bottom (the first texel row is the top).
 *
 * Decoding (all renderers' CPU path; the GPU path gives the same values up to rounding):
 *   BC1: endpoints 565 -> 888 by bit replication; c0 > c1 (or always, inside BC3): c2 = (2 c0 + c1 + 1) / 3,
 *        c3 = (c0 + 2 c1 + 1) / 3; else c2 = (c0 + c1 + 1) / 2 and c3 = transparent black.
 *   BC4 (BC3 alpha, BC5 red and green): a0 > a1: code k in 2..7 = ((8 - k) a0 + (k - 1) a1 + 3) / 7; else codes 2..5 =
 *        ((6 - k) a0 + (k - 1) a1 + 2) / 5, 6 = 0, 7 = 255.
 *   BC5: red, green from the two BC4 blocks; blue = 127.5 + 127.5 sqrt(1 - x^2 - y^2) (x, y = 2 v - 1): a normal's Z;
 *        alpha 255. (On the GPU path the shaders rebuild Z the same way when the texture is a normal map.)
 *   BC7: the D3D11 / BPTC specification, all eight modes (integer weights, (64 - w) e0 + w e1 + 32 >> 6). */

static const uint32_t arc__bc7_p2[64] = {
    0x50505050, 0x40404040, 0x54545454, 0x54505040, 0x50404000, 0x55545450, 0x55545040, 0x54504000,
    0x50400000, 0x55555450, 0x55544000, 0x54400000, 0x55555440, 0x55550000, 0x55555500, 0x55000000,
    0x55150100, 0x00004054, 0x15010000, 0x00405054, 0x00004050, 0x15050100, 0x05010000, 0x40505054,
    0x00404050, 0x05010100, 0x14141414, 0x05141450, 0x01155440, 0x00555500, 0x15014054, 0x05414150,
    0x44444444, 0x55005500, 0x11441144, 0x05055050, 0x05500550, 0x11114444, 0x41144114, 0x44111144,
    0x15055054, 0x01055040, 0x05041050, 0x05455150, 0x14414114, 0x50050550, 0x41411414, 0x00141400,
    0x00041504, 0x00105410, 0x10541000, 0x04150400, 0x50410514, 0x41051450, 0x05415014, 0x14054150,
    0x41050514, 0x41505014, 0x40011554, 0x54150140, 0x50505500, 0x00555050, 0x15151010, 0x54540404,
};
static const uint32_t arc__bc7_p3[64] = {
    0xaa685050, 0x6a5a5040, 0x5a5a4200, 0x5450a0a8, 0xa5a50000, 0xa0a05050, 0x5555a0a0, 0x5a5a5050,
    0xaa550000, 0xaa555500, 0xaaaa5500, 0x90909090, 0x94949494, 0xa4a4a4a4, 0xa9a59450, 0x2a0a4250,
    0xa5945040, 0x0a425054, 0xa5a5a500, 0x55a0a0a0, 0xa8a85454, 0x6a6a4040, 0xa4a45000, 0x1a1a0500,
    0x0050a4a4, 0xaaa59090, 0x14696914, 0x69691400, 0xa08585a0, 0xaa821414, 0x50a4a450, 0x6a5a0200,
    0xa9a58000, 0x5090a0a8, 0xa8a09050, 0x24242424, 0x00aa5500, 0x24924924, 0x24499224, 0x50a50a50,
    0x500aa550, 0xaaaa4444, 0x66660000, 0xa5a0a5a0, 0x50a050a0, 0x69286928, 0x44aaaa44, 0x66666600,
    0xaa444444, 0x54a854a8, 0x95809580, 0x96969600, 0xa85454a8, 0x80959580, 0xaa141414, 0x96960000,
    0xaaaa1414, 0xa05050a0, 0xa0a5a5a0, 0x96000000, 0x40804080, 0xa9a8a9a8, 0xaaaaaa44, 0x2a4a5254,
};
static const uint8_t arc__bc7_a2[64] = {
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 2, 8, 2, 2, 8, 8, 15, 2, 8, 2, 2, 8, 8, 2, 2,
    15, 15, 6, 8, 2, 8, 15, 15, 2, 8, 2, 2, 2, 15, 15, 6, 6, 2, 6, 8, 15, 15, 2, 2, 15, 15, 15, 15, 15, 2, 2, 15,
};
static const uint8_t arc__bc7_a3a[64] = {
    3, 3, 15, 15, 8, 3, 15, 15, 8, 8, 6, 6, 6, 5, 3, 3, 3, 3, 8, 15, 3, 3, 6, 10, 5, 8, 8, 6, 8, 5, 15, 15,
    8, 15, 3, 5, 6, 10, 8, 15, 15, 3, 15, 5, 15, 15, 15, 15, 3, 15, 5, 5, 5, 8, 5, 10, 5, 10, 8, 13, 15, 12, 3, 3,
};
static const uint8_t arc__bc7_a3b[64] = {
    15, 8, 8, 3, 15, 15, 3, 8, 15, 15, 15, 15, 15, 15, 15, 8, 15, 8, 15, 3, 15, 8, 15, 8, 3, 15, 6, 10, 15, 15, 10, 8,
    15, 3, 15, 10, 10, 8, 9, 10, 6, 15, 8, 15, 3, 6, 6, 8, 15, 3, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 3, 15, 15, 8,
};
static const uint8_t arc__bc7_w2[4] = { 0, 21, 43, 64 }, arc__bc7_w3[8] = { 0, 9, 18, 27, 37, 46, 55, 64 },
    arc__bc7_w4[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };

int arc__bc_block_bytes(int fmt) { return fmt == ARC_TEXFMT_BC1 || fmt == ARC_TEXFMT_BC4 ? 8 : 16; }
size_t arc__bc_level_bytes(int fmt, int w, int h) { return (size_t)((w + 3) / 4) * (size_t)((h + 3) / 4) * (size_t)arc__bc_block_bytes(fmt); }

static void bc_rgb565(uint32_t c, int out[3]) {
    int r = (int)(c >> 11) & 31, g = (int)(c >> 5) & 63, b = (int)c & 31;
    out[0] = (r << 3) | (r >> 2); out[1] = (g << 2) | (g >> 4); out[2] = (b << 3) | (b >> 2);
}
/* one BC1 colour block -> 16 RGBA texels (row-major, 4 bytes each); four: always the 4-colour mode (BC3) */
void arc__bc1_block(const uint8_t *b, uint8_t *out, bool four) {
    uint32_t c0 = (uint32_t)(b[0] | b[1] << 8), c1 = (uint32_t)(b[2] | b[3] << 8);
    uint32_t bits = (uint32_t)b[4] | (uint32_t)b[5] << 8 | (uint32_t)b[6] << 16 | (uint32_t)b[7] << 24;
    int p[4][4], e0[3], e1[3]; bc_rgb565(c0, e0); bc_rgb565(c1, e1);
    for (int k = 0; k < 3; k++) {
        p[0][k] = e0[k]; p[1][k] = e1[k];
        if (four || c0 > c1) { p[2][k] = (2 * e0[k] + e1[k] + 1) / 3; p[3][k] = (e0[k] + 2 * e1[k] + 1) / 3; }
        else { p[2][k] = (e0[k] + e1[k] + 1) / 2; p[3][k] = 0; }
    }
    p[0][3] = p[1][3] = p[2][3] = 255; p[3][3] = (four || c0 > c1) ? 255 : 0;
    for (int i = 0; i < 16; i++) { const int *c = p[(bits >> (2 * i)) & 3]; for (int k = 0; k < 4; k++) out[i * 4 + k] = (uint8_t)c[k]; }
}
/* one BC4 block -> 16 values written to out[i * stride] */
void arc__bc4_block(const uint8_t *b, uint8_t *out, int stride) {
    int a0 = b[0], a1 = b[1], v[8]; v[0] = a0; v[1] = a1;
    if (a0 > a1) for (int k = 2; k < 8; k++) v[k] = ((8 - k) * a0 + (k - 1) * a1 + 3) / 7;
    else { for (int k = 2; k < 6; k++) v[k] = ((6 - k) * a0 + (k - 1) * a1 + 2) / 5; v[6] = 0; v[7] = 255; }
    uint64_t bits = 0; for (int i = 0; i < 6; i++) bits |= (uint64_t)b[2 + i] << (8 * i);
    for (int i = 0; i < 16; i++) out[i * stride] = (uint8_t)v[(bits >> (3 * i)) & 7];
}
/* BC5 normal map: Z from X and Y (see the top of this file) */
static uint8_t bc_normal_z(int x8, int y8) {
    float x = (float)x8 / 127.5f - 1.0f, y = (float)y8 / 127.5f - 1.0f, z2 = 1.0f - x * x - y * y;
    float z = z2 > 0 ? sqrtf(z2) : 0.0f; int v = (int)(127.5f + 127.5f * z + 0.5f);
    return (uint8_t)(v > 255 ? 255 : v);
}
void arc__bc5_block(const uint8_t *b, uint8_t *out) {
    arc__bc4_block(b, out, 4); arc__bc4_block(b + 8, out + 1, 4);
    for (int i = 0; i < 16; i++) { out[i * 4 + 2] = bc_normal_z(out[i * 4], out[i * 4 + 1]); out[i * 4 + 3] = 255; }
}

/* ---- BC7 ---- */
typedef struct { const uint8_t *b; int at; } bc7_bits;
static int bc7_get(bc7_bits *r, int n) {
    int v = 0;
    for (int i = 0; i < n; i++, r->at++) v |= ((r->b[r->at >> 3] >> (r->at & 7)) & 1) << i;
    return v;
}
static int bc7_expand(int v, int bits) { v <<= 8 - bits; return v | (v >> bits); }
void arc__bc7_block(const uint8_t *b, uint8_t *out) {
    /* per mode: subsets, partition bits, rotation bits, index selection bit, colour bits, alpha bits, endpoint p-bits,
       shared p-bits, index bits, second index bits */
    static const uint8_t M[8][10] = {
        { 3, 4, 0, 0, 4, 0, 1, 0, 3, 0 }, { 2, 6, 0, 0, 6, 0, 0, 1, 3, 0 }, { 3, 6, 0, 0, 5, 0, 0, 0, 2, 0 },
        { 2, 6, 0, 0, 7, 0, 1, 0, 2, 0 }, { 1, 0, 2, 1, 5, 6, 0, 0, 2, 3 }, { 1, 0, 2, 0, 7, 8, 0, 0, 2, 2 },
        { 1, 0, 0, 0, 7, 7, 1, 0, 4, 0 }, { 2, 6, 0, 0, 5, 5, 1, 0, 2, 0 } };
    int mode = 0; while (mode < 8 && !((b[0] >> mode) & 1)) mode++;
    if (mode == 8) { memset(out, 0, 64); return; }                  /* reserved: transparent black */
    const uint8_t *m = M[mode]; bc7_bits r = { b, mode + 1 };
    int ns = m[0], part = bc7_get(&r, m[1]), rot = bc7_get(&r, m[2]), isel = bc7_get(&r, m[3]);
    int e[6][4];                                                    /* endpoints: subset s uses e[2s], e[2s + 1] */
    for (int c = 0; c < 3; c++) for (int i = 0; i < ns * 2; i++) e[i][c] = bc7_get(&r, m[4]);
    for (int i = 0; i < ns * 2; i++) e[i][3] = m[5] ? bc7_get(&r, m[5]) : 255;
    int cb = m[4], ab = m[5];
    if (m[6]) {                                                     /* a p-bit per endpoint */
        for (int i = 0; i < ns * 2; i++) { int p = bc7_get(&r, 1); for (int c = 0; c < 3; c++) e[i][c] = e[i][c] << 1 | p; if (ab) e[i][3] = e[i][3] << 1 | p; }
        cb++; if (ab) ab++;
    } else if (m[7]) {                                              /* a p-bit per subset */
        for (int s = 0; s < ns; s++) { int p = bc7_get(&r, 1); for (int j = 0; j < 2; j++) for (int c = 0; c < 3; c++) e[s * 2 + j][c] = e[s * 2 + j][c] << 1 | p; }
        cb++;
    }
    for (int i = 0; i < ns * 2; i++) {
        for (int c = 0; c < 3; c++) e[i][c] = bc7_expand(e[i][c], cb);
        e[i][3] = ab ? bc7_expand(e[i][3], ab) : 255;
    }
    uint32_t pmap = ns == 2 ? arc__bc7_p2[part] : ns == 3 ? arc__bc7_p3[part] : 0;
    int idx[16], idx2[16];
    for (int i = 0; i < 16; i++) {
        int s = (int)(pmap >> (2 * i)) & 3;
        bool anchor = i == 0 || (ns == 2 && s == 1 && i == arc__bc7_a2[part]) || (ns == 3 && ((s == 1 && i == arc__bc7_a3a[part]) || (s == 2 && i == arc__bc7_a3b[part])));
        idx[i] = bc7_get(&r, m[8] - (anchor ? 1 : 0));
    }
    if (m[9]) for (int i = 0; i < 16; i++) idx2[i] = bc7_get(&r, m[9] - (i == 0 ? 1 : 0));
    for (int i = 0; i < 16; i++) {
        int s = (int)(pmap >> (2 * i)) & 3; const int *a = e[s * 2], *c = e[s * 2 + 1];
        int ci = idx[i], cbits = m[8], ai = idx[i], abits = m[8];
        if (m[9]) { if (isel) { ci = idx2[i]; cbits = m[9]; } else { ai = idx2[i]; abits = m[9]; } }
        const uint8_t *wc = cbits == 2 ? arc__bc7_w2 : cbits == 3 ? arc__bc7_w3 : arc__bc7_w4, *wa = abits == 2 ? arc__bc7_w2 : abits == 3 ? arc__bc7_w3 : arc__bc7_w4;
        uint8_t *o = out + i * 4;
        for (int k = 0; k < 3; k++) o[k] = (uint8_t)(((64 - wc[ci]) * a[k] + wc[ci] * c[k] + 32) >> 6);
        o[3] = (uint8_t)(((64 - wa[ai]) * a[3] + wa[ai] * c[3] + 32) >> 6);
        if (rot) { uint8_t t = o[3]; o[3] = o[rot - 1]; o[rot - 1] = t; }
    }
}

/* a whole level -> RGBA (rgba is w * h * 4 bytes) */
bool arc__bc_decode(int fmt, const uint8_t *data, size_t size, int w, int h, uint8_t *rgba) {
    if (w <= 0 || h <= 0 || size < arc__bc_level_bytes(fmt, w, h)) return false;
    int bw = (w + 3) / 4, bh = (h + 3) / 4, bb = arc__bc_block_bytes(fmt); uint8_t px[64];
    for (int by = 0; by < bh; by++)
        for (int bx = 0; bx < bw; bx++) {
            const uint8_t *b = data + ((size_t)by * bw + bx) * bb;
            switch (fmt) {
            case ARC_TEXFMT_BC1: arc__bc1_block(b, px, false); break;
            case ARC_TEXFMT_BC3: arc__bc1_block(b + 8, px, true); arc__bc4_block(b, px + 3, 4); break;
            case ARC_TEXFMT_BC4: arc__bc4_block(b, px, 4); for (int i = 0; i < 16; i++) { px[i * 4 + 1] = px[i * 4 + 2] = 0; px[i * 4 + 3] = 255; } break;
            case ARC_TEXFMT_BC5: arc__bc5_block(b, px); break;
            case ARC_TEXFMT_BC7: arc__bc7_block(b, px); break;
            default: return false;
            }
            for (int y = 0; y < 4 && by * 4 + y < h; y++)
                for (int x = 0; x < 4 && bx * 4 + x < w; x++) memcpy(rgba + ((size_t)(by * 4 + y) * w + (size_t)(bx * 4 + x)) * 4, px + (y * 4 + x) * 4, 4);
        }
    return true;
}

/* ---- the .arct container ---- */
bool arc__ctex_open(const uint8_t *d, size_t n, arc__ctex *t) {
    memset(t, 0, sizeof *t);
    if (!d || n < 32 || memcmp(d, "ARCT", 4)) return false;
    uint32_t h[7]; memcpy(h, d + 4, sizeof h);
    uint32_t ver = h[0], fmt = h[1], w = h[2], hh = h[3], levels = h[4], flags = h[5], stored = h[6];
    if (ver != 1 || (fmt != ARC_TEXFMT_BC1 && fmt != ARC_TEXFMT_BC3 && fmt != ARC_TEXFMT_BC4 && fmt != ARC_TEXFMT_BC5 && fmt != ARC_TEXFMT_BC7)) return false;
    if (!w || !hh || w > 16384 || hh > 16384 || !levels || levels > ARC__MAX_MIPS || stored > n - 32) return false;
    size_t total = 0; int lw = (int)w, lh = (int)hh;
    for (uint32_t i = 0; i < levels; i++) {
        t->off[i] = total; t->lw[i] = lw; t->lh[i] = lh; total += arc__bc_level_bytes((int)fmt, lw, lh);
        lw = lw > 1 ? lw / 2 : 1; lh = lh > 1 ? lh / 2 : 1;
    }
    if (flags & 2) {
        size_t got = 0; t->owned = arc__inflate(d + 32, stored, total, &got);
        if (!t->owned || got < total) { free(t->owned); t->owned = NULL; return false; }
        t->data = t->owned;
    } else {
        if (stored < total) return false;
        t->data = d + 32;
    }
    t->fmt = (int)fmt; t->w = (int)w; t->h = (int)hh; t->levels = (int)levels; t->flags = (int)flags; t->bytes = total;
    return true;
}
void arc__ctex_free(arc__ctex *t) { free(t->owned); t->owned = NULL; t->data = NULL; }
size_t arc__ctex_level_bytes(const arc__ctex *t, int level) { return arc__bc_level_bytes(t->fmt, t->lw[level], t->lh[level]); }
uint8_t *arc__ctex_decode(const arc__ctex *t, int level) {
    if (level < 0 || level >= t->levels) return NULL;
    uint8_t *px = (uint8_t *)malloc((size_t)t->lw[level] * (size_t)t->lh[level] * 4);
    if (px && !arc__bc_decode(t->fmt, t->data + t->off[level], arc__ctex_level_bytes(t, level), t->lw[level], t->lh[level], px)) { free(px); px = NULL; }
    return px;
}
