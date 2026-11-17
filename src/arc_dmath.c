/* ARC 0.8 deterministic maths: float functions computed by ARC itself (no libm), bit-identical on every compiler and
   platform ARC builds for (gcc, clang, MinGW, MSVC x64, Emscripten/wasm). They use only IEEE double operations (+ - * /,
   and sqrt, which IEEE makes exact), integer arithmetic and bit tricks, so the result depends on nothing but the input.
   Accuracy: within 1 ulp of the correctly rounded result (most results are correctly rounded): the work is done in double
   and rounded to float once at the end.
   Requirements: SSE2 or wasm floating point (any x86-64, ARM64 or wasm target; not x87), no -ffast-math, no fused
   multiply-add contraction (build.sh uses -ffp-contract=off; clang and MSVC are told so below as well).
   ARC's own helpers (arc_m4_rotate_*, quaternions, the renderers) keep using the C library, so old games draw exactly as
   before: a multiplayer game uses these for anything that changes game state. */
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif

static inline uint32_t dm_fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static inline float    dm_bitsf(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static inline uint64_t dm_dbits(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
static inline double   dm_bitsd(uint64_t u) { double d; memcpy(&d, &u, 8); return d; }
static inline double   dm_pow2(int k) { return dm_bitsd((uint64_t)(k + 1023) << 52); }   /* -1022 <= k <= 1023 */

/* ---- range reduction for sin/cos/tan: x = n * pi/2 + y, |y| <= ~pi/4 ---- */
static const uint32_t dm_2opi[12] = {   /* bits of 2/pi */
    0xA2F9836Eu, 0x4E441529u, 0xFC2757D1u, 0xF534DDC0u, 0xDB629599u, 0x3C439041u,
    0xFE5163ABu, 0xDEBBC561u, 0xB7246E3Au, 0x424DD2E0u, 0x06492EEAu, 0x09D1921Cu };
static uint32_t dm_2opi_at(int pos) {   /* 32 bits of 2/pi starting at bit `pos` (0 = the first bit after the point) */
    int w = pos >> 5, s = pos & 31;
    return s ? (dm_2opi[w] << s) | (dm_2opi[w + 1] >> (32 - s)) : dm_2opi[w];
}
static int dm_rem_pio2(float x, double *y) {
    uint32_t ix = dm_fbits(x) & 0x7FFFFFFFu;
    double xd = (double)x;
    if (ix < 0x49000000u) {             /* |x| < 2^19: Cody-Waite in double (n * pio2_1 is exact) */
        const double invpio2 = 0.6366197723675814, pio2_1 = 1.5707963109016418, pio2_1t = 1.5893254773528196e-08;
        double t = xd * invpio2;
        int n = (int)(t + (t >= 0 ? 0.5 : -0.5));
        double fn = (double)n;
        *y = (xd - fn * pio2_1) - fn * pio2_1t;
        return n;
    }
    /* Payne-Hanek: |x| = M * 2^E (M 24 bits); (x * 2/pi) mod 4 from a 96-bit window of 2/pi's bits */
    int e = (int)(ix >> 23) - 127, E = e - 23;
    uint64_t M = (uint64_t)((ix & 0x7FFFFFu) | 0x800000u);
    int i0 = E - 1 > 1 ? E - 1 : 1;     /* bits before i0 add whole multiples of 4 */
    uint64_t w0 = dm_2opi_at(i0 - 1), w1 = dm_2opi_at(i0 + 31), w2 = dm_2opi_at(i0 + 63);
    uint64_t p0 = M * w0, p1 = M * w1, p2 = M * w2;
    uint64_t lo = p2 + (p1 << 32), hi = (p1 >> 32) + p0 + (lo < p2 ? 1u : 0u);
    int k = 95 + i0 - E;                /* Q = hi:lo; x * 2/pi mod 4 = Q / 2^k, 94 <= k <= 100 */
    int quad = (int)((hi >> (k - 64)) & 3u), s = 128 - k;
    uint64_t frac = (hi << s) | (lo >> (64 - s));
    double r = (double)frac * 5.421010862427522e-20;   /* 2^-64 */
    if (r >= 0.5) { r -= 1.0; quad++; }
    double yy = r * 1.5707963267948966;
    if (dm_fbits(x) >> 31) { *y = -yy; return -quad; }
    *y = yy; return quad;
}
static double dm_sin_k(double x) {      /* |x| <= ~pi/4: fdlibm's double kernel (full double precision) */
    const double S1 = -1.66666666666666324348e-01, S2 = 8.33333333332248946124e-03, S3 = -1.98412698298579493134e-04,
                 S4 = 2.75573137070700676789e-06, S5 = -2.50507602534068634195e-08, S6 = 1.58969099521155010221e-10;
    double z = x * x, v = z * x, r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
    return x + v * (S1 + z * r);
}
static double dm_cos_k(double x) {
    const double C1 = 4.16666666666666019037e-02, C2 = -1.38888888888741095749e-03, C3 = 2.48015872894767294178e-05,
                 C4 = -2.75573143513906633035e-07, C5 = 2.08757232129817482790e-09, C6 = -1.13596475577881948265e-11;
    double z = x * x, r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6))))), hz = 0.5 * z, w = 1.0 - hz;
    return w + (((1.0 - w) - hz) + z * r);
}
static double dm_tan_k(double x, int odd) { double s = dm_sin_k(x), c = dm_cos_k(x); return odd ? -c / s : s / c; }
float arc_sinf(float x) {
    uint32_t ix = dm_fbits(x) & 0x7FFFFFFFu;
    if (ix >= 0x7F800000u) return x - x;                    /* inf, NaN: NaN */
    if (ix < 0x39800000u) return x;                         /* |x| < 2^-12: sin x = x to float precision */
    if (ix <= 0x3F490FDAu) return (float)dm_sin_k((double)x);   /* |x| <= pi/4 */
    double y; int n = dm_rem_pio2(x, &y);
    switch (n & 3) { case 0: return (float)dm_sin_k(y); case 1: return (float)dm_cos_k(y); case 2: return (float)-dm_sin_k(y); default: return (float)-dm_cos_k(y); }
}
float arc_cosf(float x) {
    uint32_t ix = dm_fbits(x) & 0x7FFFFFFFu;
    if (ix >= 0x7F800000u) return x - x;
    if (ix < 0x39800000u) return 1.0f;
    if (ix <= 0x3F490FDAu) return (float)dm_cos_k((double)x);
    double y; int n = dm_rem_pio2(x, &y);
    switch (n & 3) { case 0: return (float)dm_cos_k(y); case 1: return (float)-dm_sin_k(y); case 2: return (float)-dm_cos_k(y); default: return (float)dm_sin_k(y); }
}
float arc_tanf(float x) {
    uint32_t ix = dm_fbits(x) & 0x7FFFFFFFu;
    if (ix >= 0x7F800000u) return x - x;
    if (ix < 0x39800000u) return x;
    if (ix <= 0x3F490FDAu) return (float)dm_tan_k((double)x, 0);
    double y; int n = dm_rem_pio2(x, &y);
    return (float)dm_tan_k(y, n & 1);
}

/* ---- atan, atan2, asin, acos (fdlibm's double atan, rounded to float) ---- */
static double dm_atan(double x) {
    static const double hi[4] = { 4.63647609000806093515e-01, 7.85398163397448278999e-01, 9.82793723247329054082e-01, 1.57079632679489655800e+00 };
    static const double lo[4] = { 2.26987774529616870924e-17, 3.06161699786838301793e-17, 1.39033110312309984516e-17, 6.12323399573676603587e-17 };
    static const double aT[11] = { 3.33333333333329318027e-01, -1.99999999998764832476e-01, 1.42857142725034663711e-01, -1.11111104054623557880e-01,
        9.09088713343650656196e-02, -7.69187620504482999495e-02, 6.66107313738753120669e-02, -5.83357013379057348645e-02,
        4.97687799461593236017e-02, -3.65315727442169155270e-02, 1.62858201153657823623e-02 };
    if (x != x) return x;
    bool neg = x < 0; double a = neg ? -x : x;
    int id;
    if (a >= 7.378697629483821e19) { double r = hi[3] + lo[3]; return neg ? -r : r; }   /* 2^66 */
    if (a < 0.4375) { if (a < 1.862645149230957e-09) return x; id = -1; }
    else if (a < 1.1875) { if (a < 0.6875) { id = 0; a = (2.0 * a - 1.0) / (2.0 + a); } else { id = 1; a = (a - 1.0) / (a + 1.0); } }
    else if (a < 2.4375) { id = 2; a = (a - 1.5) / (1.0 + 1.5 * a); }
    else { id = 3; a = -1.0 / a; }
    double xx = id < 0 ? x : a;
    double z = xx * xx, w = z * z;
    double s1 = z * (aT[0] + w * (aT[2] + w * (aT[4] + w * (aT[6] + w * (aT[8] + w * aT[10])))));
    double s2 = w * (aT[1] + w * (aT[3] + w * (aT[5] + w * (aT[7] + w * aT[9]))));
    if (id < 0) return xx - xx * (s1 + s2);
    double r = hi[id] - ((xx * (s1 + s2) - lo[id]) - xx);
    return neg ? -r : r;
}
static double dm_atan2(double y, double x) {
    const double pi = 3.141592653589793, pio2 = 1.5707963267948966, pio4 = 0.7853981633974483;
    if (x != x || y != y) return x + y;
    bool yneg = (dm_dbits(y) >> 63) != 0, xneg = (dm_dbits(x) >> 63) != 0;
    double inf = dm_bitsd(0x7FF0000000000000ull);
    if (y == 0) return xneg ? (yneg ? -pi : pi) : y;
    if (x == 0) return yneg ? -pio2 : pio2;
    if (x == inf || x == -inf) {
        if (y == inf || y == -inf) { double r = xneg ? 3.0 * pio4 : pio4; return yneg ? -r : r; }
        double r = xneg ? pi : 0.0; return yneg ? -r : r;
    }
    if (y == inf || y == -inf) return yneg ? -pio2 : pio2;
    double ay = yneg ? -y : y, ax = xneg ? -x : x;
    double z = dm_atan(ay / ax);
    if (xneg) z = pi - z;
    return yneg ? -z : z;
}
float arc_atanf(float x) { return (float)dm_atan((double)x); }
float arc_atan2f(float y, float x) { return (float)dm_atan2((double)y, (double)x); }
float arc_asinf(float x) {
    double d = (double)x;
    if (!(d >= -1.0 && d <= 1.0)) return (x - x) / (x - x);   /* NaN (also for NaN in) */
    return (float)dm_atan2(d, sqrt((1.0 - d) * (1.0 + d)));
}
float arc_acosf(float x) {
    double d = (double)x;
    if (!(d >= -1.0 && d <= 1.0)) return (x - x) / (x - x);
    return (float)dm_atan2(sqrt((1.0 - d) * (1.0 + d)), d);
}
float arc_sqrtf(float x) { return sqrtf(x); }   /* IEEE square root is exactly rounded everywhere */
float arc_hypotf(float x, float y) {
    uint32_t ax = dm_fbits(x) & 0x7FFFFFFFu, ay = dm_fbits(y) & 0x7FFFFFFFu;
    if (ax == 0x7F800000u || ay == 0x7F800000u) return dm_bitsf(0x7F800000u);
    double a = (double)x, b = (double)y;
    return (float)sqrt(a * a + b * b);           /* both squares are exact in double */
}

/* ---- exp and log in double (fdlibm-style), for expf, logf, powf ---- */
static double dm_exp(double x) {               /* -110 <= x <= 90: float results only */
    const double log2e = 1.4426950408889634, ln2hi = 6.93147180369123816490e-01, ln2lo = 1.90821492927058770002e-10;
    double t = x * log2e;
    int k = (int)(t + (t >= 0 ? 0.5 : -0.5));
    double r = (x - (double)k * ln2hi) - (double)k * ln2lo;   /* |r| <= 0.35 */
    double p = 1.0 / 6227020800.0;                             /* Taylor to r^13 / 13! */
    p = 1.0 / 479001600.0 + r * p; p = 1.0 / 39916800.0 + r * p; p = 1.0 / 3628800.0 + r * p; p = 1.0 / 362880.0 + r * p;
    p = 1.0 / 40320.0 + r * p; p = 1.0 / 5040.0 + r * p; p = 1.0 / 720.0 + r * p; p = 1.0 / 120.0 + r * p;
    p = 1.0 / 24.0 + r * p; p = 1.0 / 6.0 + r * p; p = 0.5 + r * p; p = 1.0 + r * p; p = 1.0 + r * p;
    return p * dm_pow2(k);
}
static double dm_log(double x) {               /* x > 0, finite */
    const double ln2_hi = 6.93147180369123816490e-01, ln2_lo = 1.90821492927058770002e-10;
    const double Lg1 = 6.666666666666735130e-01, Lg2 = 3.999999999940941908e-01, Lg3 = 2.857142874366239149e-01, Lg4 = 2.222219843214978396e-01,
                 Lg5 = 1.818357216161805012e-01, Lg6 = 1.531383769920937332e-01, Lg7 = 1.479819860511658591e-01;
    uint64_t u = dm_dbits(x);
    int k = (int)((u >> 52) & 0x7FF) - 1023;
    if (k == -1023) { x *= 18014398509481984.0; u = dm_dbits(x); k = (int)((u >> 52) & 0x7FF) - 1023 - 54; }   /* subnormal: scale by 2^54 */
    double m = dm_bitsd((u & 0x000FFFFFFFFFFFFFull) | 0x3FF0000000000000ull);   /* [1, 2) */
    if (m > 1.4142135623730951) { m *= 0.5; k++; }
    double f = m - 1.0, hfsq = 0.5 * f * f, s = f / (2.0 + f), z = s * s, w = z * z;
    double t1 = w * (Lg2 + w * (Lg4 + w * Lg6)), t2 = z * (Lg1 + w * (Lg3 + w * (Lg5 + w * Lg7))), R = t2 + t1, dk = (double)k;
    return dk * ln2_hi - ((hfsq - (s * (hfsq + R) + dk * ln2_lo)) - f);
}
static float dm_exp_clamped(double t) {        /* exp(t) as a float, with overflow / underflow */
    if (t != t) return (float)t;
    if (t > 90.0) return dm_bitsf(0x7F800000u);
    if (t < -110.0) return 0.0f;
    return (float)dm_exp(t);
}
float arc_expf(float x) { return dm_exp_clamped((double)x); }
float arc_exp2f(float x) { return dm_exp_clamped((double)x * 0.6931471805599453); }
static bool dm_log_special(float x, float *out) {
    uint32_t u = dm_fbits(x);
    if ((u & 0x7FFFFFFFu) > 0x7F800000u) { *out = x; return true; }               /* NaN */
    if ((u & 0x7FFFFFFFu) == 0) { *out = dm_bitsf(0xFF800000u); return true; }      /* +-0: -inf */
    if (u >> 31) { *out = (x - x) / (x - x); return true; }                       /* negative: NaN */
    if (u == 0x7F800000u) { *out = x; return true; }                                /* +inf */
    return false;
}
float arc_logf(float x) { float r; if (dm_log_special(x, &r)) return r; return (float)dm_log((double)x); }
float arc_log2f(float x) { float r; if (dm_log_special(x, &r)) return r; return (float)(dm_log((double)x) * 1.4426950408889634); }
float arc_log10f(float x) { float r; if (dm_log_special(x, &r)) return r; return (float)(dm_log((double)x) * 0.4342944819032518); }
float arc_fabsf(float x) { return dm_bitsf(dm_fbits(x) & 0x7FFFFFFFu); }
float arc_floorf(float x) {
    uint32_t u = dm_fbits(x); int e = (int)(u >> 23 & 0xFF) - 0x7F;
    if (e >= 23) return x;
    if (e >= 0) { uint32_t m = 0x007FFFFFu >> e; if ((u & m) == 0) return x; if (u >> 31) u += m; u &= ~m; }
    else { if (u >> 31 == 0) u = 0; else if (u << 1) u = 0xBF800000u; }   /* -1 */
    return dm_bitsf(u);
}
float arc_ceilf(float x) {
    uint32_t u = dm_fbits(x); int e = (int)(u >> 23 & 0xFF) - 0x7F;
    if (e >= 23) return x;
    if (e >= 0) { uint32_t m = 0x007FFFFFu >> e; if ((u & m) == 0) return x; if (u >> 31 == 0) u += m; u &= ~m; }
    else { if (u >> 31) u = 0x80000000u; else if (u << 1) u = 0x3F800000u; }   /* -0, 1 */
    return dm_bitsf(u);
}
float arc_truncf(float x) {
    uint32_t u = dm_fbits(x); int e = (int)(u >> 23 & 0xFF) - 0x7F + 9;
    if (e >= 32) return x;
    if (e < 9) e = 1;
    uint32_t m = 0xFFFFFFFFu >> e;
    if ((u & m) == 0) return x;
    return dm_bitsf(u & ~m);
}
float arc_roundf(float x) {                    /* halfway cases away from zero */
    if ((dm_fbits(x) & 0x7FFFFFFFu) >= 0x4B000000u) return x;   /* |x| >= 2^23 (or inf, NaN): already whole */
    float t = arc_truncf(x), d = arc_fabsf(x - t);           /* exact */
    if (d >= 0.5f) t += (dm_fbits(x) >> 31) ? -1.0f : 1.0f;
    return t;
}
float arc_fmodf(float x, float y) {            /* exact (musl's integer algorithm) */
    uint32_t ux = dm_fbits(x), uy = dm_fbits(y);
    int ex = (int)(ux >> 23 & 0xFF), ey = (int)(uy >> 23 & 0xFF);
    uint32_t sx = ux & 0x80000000u, i;
    if ((uy << 1) == 0 || y != y || ex == 0xFF) return (x * y) / (x * y);
    if ((ux << 1) <= (uy << 1)) { if ((ux << 1) == (uy << 1)) return 0.0f * x; return x; }
    if (!ex) { for (i = ux << 9; i >> 31 == 0; ex--, i <<= 1) {} ux <<= (uint32_t)(-ex + 1); }
    else { ux &= 0xFFFFFFFFu >> 9; ux |= 1u << 23; }
    if (!ey) { for (i = uy << 9; i >> 31 == 0; ey--, i <<= 1) {} uy <<= (uint32_t)(-ey + 1); }
    else { uy &= 0xFFFFFFFFu >> 9; uy |= 1u << 23; }
    for (; ex > ey; ex--) { i = ux - uy; if (i >> 31 == 0) { if (i == 0) return 0.0f * x; ux = i; } ux <<= 1; }
    i = ux - uy; if (i >> 31 == 0) { if (i == 0) return 0.0f * x; ux = i; }
    for (; ux >> 23 == 0; ux <<= 1, ex--) {}
    if (ex > 0) { ux -= 1u << 23; ux |= (uint32_t)ex << 23; } else ux >>= (uint32_t)(-ex + 1);
    return dm_bitsf(ux | sx);
}
static int dm_int_kind(float y) {              /* 0 not an integer, 1 even, 2 odd */
    if (arc_floorf(y) != y) return 0;
    float a = arc_fabsf(y);
    if (a >= 16777216.0f) return 1;            /* 2^24 and up: every float is even */
    return ((int32_t)a & 1) ? 2 : 1;
}
float arc_powf(float x, float y) {
    uint32_t ux = dm_fbits(x), uy = dm_fbits(y), ax = ux & 0x7FFFFFFFu, ay = uy & 0x7FFFFFFFu;
    float inf = dm_bitsf(0x7F800000u);
    if (ay == 0) return 1.0f;
    if (ux == 0x3F800000u) return 1.0f;
    if (ax > 0x7F800000u || ay > 0x7F800000u) return x + y;            /* NaN */
    bool yneg = (uy >> 31) != 0, xneg = (ux >> 31) != 0;
    if (ay == 0x7F800000u) {                                             /* y = +-inf */
        if (ax == 0x3F800000u) return 1.0f;                              /* x = -1 */
        bool big = ax > 0x3F800000u;
        return (big != yneg) ? inf : 0.0f;
    }
    int yk = dm_int_kind(y);
    if (ax == 0) {                                                        /* x = +-0 */
        if (yneg) return (yk == 2 && xneg) ? -inf : inf;
        return (yk == 2 && xneg) ? -0.0f : 0.0f;
    }
    if (ax == 0x7F800000u) {                                             /* x = +-inf */
        if (xneg) { if (yneg) return yk == 2 ? -0.0f : 0.0f; return yk == 2 ? -inf : inf; }
        return yneg ? 0.0f : inf;
    }
    if (xneg && yk == 0) return (x - x) / (x - x);                       /* negative to a fractional power: NaN */
    double t = (double)y * dm_log((double)dm_bitsf(ax));
    float r = dm_exp_clamped(t);
    return (xneg && yk == 2) ? -r : r;
}
