#!/usr/bin/env python3
"""Block-compress textures for ARC (0.7.1): .arct files, and the textures inside .arcm meshes.

    python3 tools/texcompress.py in.png out.arct [--format auto|bc1|bc3|bc5|bc7] [--normal] [--no-mips]
    python3 tools/texcompress.py --arcm in.arcm out.arcm [options]          recompress a mesh's texture and material maps
    python3 tools/texcompress.py --arcm-batch SRC_DIR DST_DIR [options]     every .arcm in a folder (others copied as is)
    python3 tools/texcompress.py --report in.png [--format ...]             quality (PSNR) of this encoder and a naive one

Formats (GPU block compression; ARC uploads them as they are, or decodes them on the CPU where a GPU can't take them):
  bc1   RGB, 4 bits a texel (8x smaller than RGBA8); pixels with alpha below 128 become cut-out (1-bit alpha)
  bc3   RGBA, 8 bits a texel: BC1 colour + a separate 8-level alpha block
  bc5   two channels (X, Y of a normal map; ARC rebuilds Z), 8 bits a texel
  bc7   RGBA, 8 bits a texel, best quality (this encoder uses mode 6: one RGBA line per block, 16 levels)
  auto  colour: bc1 when alpha is all opaque or all 0/255 cut-outs, else bc3; --normal: bc5

Options:
  --normal          the image is a tangent-space normal map (mips are renormalised; auto picks bc5)
  --no-mips         only the full-size level (default: the whole chain down to 1x1, box-filtered before compressing)
  --no-zlib         store the blocks raw (default: zlib-compressed when that saves 5% or more; ARC inflates at load)
  --naive           the simplest encoder (bounding-box endpoints, no refinement): only for comparison
  --color-format F  --arcm: the colour texture's format (auto | bc1 | bc3 | bc7)     default auto
  --normal-format F --arcm: the normal map's format (bc5 | bc3 | bc1 | keep)         default bc5
  --map-format F    --arcm: occlusion-roughness-metal and emissive maps (bc1 | bc7 | keep)   default bc1
  --max-size N      --arcm: shrink images larger than N first (0 = keep)

Images must be at least 4x4 with sides that are multiples of 4 to be compressed (the GPUs' rule); --arcm keeps any other
image as it is. Needs numpy and Pillow.

Encoder: per 4x4 block, the principal axis of the colours (PCA) gives the first endpoints; then a few rounds of
least-squares endpoint fitting for the chosen indices and a +-1 search on the quantised endpoints, keeping the best
(cluster-fit quality at range-fit cost, vectorised over all blocks). Alpha / BC4 blocks try both of BC4's modes.

.arct layout: see src/arc_texc.c ("ARCT", version 1, format, width, height, levels, flags, payload bytes, payload).
Decoding (also below, to measure quality) follows src/arc_texc.c exactly.
"""
import argparse, io, os, struct, sys, zlib

FMT = {'bc1': 1, 'bc3': 3, 'bc4': 4, 'bc5': 5, 'bc7': 7}
FMT_NAME = {v: k for k, v in FMT.items()}
BLOCK_BYTES = {1: 8, 3: 16, 4: 8, 5: 16, 7: 16}


def need_np():
    try:
        import numpy
        return numpy
    except ImportError:
        sys.exit('texcompress needs numpy (pip install numpy pillow)')


np = need_np()

# ---------------------------------------------------------------------------------------------------------------
# blocks
# ---------------------------------------------------------------------------------------------------------------

def to_blocks(img):
    """HxWxC uint8 -> (N, 16, C) blocks, row-major blocks and texels (edges replicated to fill partial blocks)"""
    h, w, c = img.shape
    ph, pw = (-h) % 4, (-w) % 4
    if ph or pw: img = np.pad(img, ((0, ph), (0, pw), (0, 0)), mode='edge')
    H, W = img.shape[0] // 4, img.shape[1] // 4
    return img.reshape(H, 4, W, 4, c).transpose(0, 2, 1, 3, 4).reshape(H * W, 16, c)


def from_blocks(blocks, w, h):
    H, W = (h + 3) // 4, (w + 3) // 4
    c = blocks.shape[2]
    return blocks.reshape(H, W, 4, 4, c).transpose(0, 2, 1, 3, 4).reshape(H * 4, W * 4, c)[:h, :w]


def mip_chain(img, normal=False, levels=True):
    """levels of an HxWx4 uint8 image: 2x2 box filter (as ARC's own CPU mips: odd sides clamp), normals renormalised"""
    out = [img]
    cur = img.astype(np.float64)
    while levels and (cur.shape[0] > 1 or cur.shape[1] > 1):
        h, w = cur.shape[:2]
        nh, nw = max(h // 2, 1), max(w // 2, 1)
        y0 = np.minimum(np.arange(nh) * 2, h - 1); y1 = np.minimum(np.arange(nh) * 2 + 1, h - 1)
        x0 = np.minimum(np.arange(nw) * 2, w - 1); x1 = np.minimum(np.arange(nw) * 2 + 1, w - 1)
        cur = (cur[y0][:, x0] + cur[y0][:, x1] + cur[y1][:, x0] + cur[y1][:, x1]) / 4.0
        if normal:
            v = cur[..., :3] / 127.5 - 1.0
            n = np.linalg.norm(v, axis=2, keepdims=True)
            v = np.where(n > 1e-6, v / np.maximum(n, 1e-6), np.array([0.0, 0.0, 1.0]))
            cur = cur.copy(); cur[..., :3] = (v + 1.0) * 127.5
        out.append(np.clip(np.round(cur), 0, 255).astype(np.uint8))
    return out

# ---------------------------------------------------------------------------------------------------------------
# palettes (exactly as src/arc_texc.c decodes them)
# ---------------------------------------------------------------------------------------------------------------

def unpack565(c):
    c = c.astype(np.int64)
    r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
    return np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], -1)


def pack565(q):   # q: (..., 3) ints 0..31/63/31
    return (q[..., 0] << 11) | (q[..., 1] << 5) | q[..., 2]


def bc1_palette(c0, c1, four=None):
    """c0, c1: (N,) uint16 -> (N, 4, 4) RGBA palette; four: force the 4-colour mode (BC3)"""
    e0, e1 = unpack565(c0), unpack565(c1)
    m4 = (c0 > c1) if four is None else np.ones(c0.shape, bool)
    p = np.zeros(c0.shape + (4, 4), np.int64)
    p[:, 0, :3], p[:, 1, :3] = e0, e1
    p[:, 2, :3] = np.where(m4[:, None], (2 * e0 + e1 + 1) // 3, (e0 + e1 + 1) // 2)
    p[:, 3, :3] = np.where(m4[:, None], (e0 + 2 * e1 + 1) // 3, 0)
    p[:, :, 3] = 255
    p[:, 3, 3] = np.where(m4, 255, 0)
    return p


def bc4_palette(a0, a1):
    a0 = a0.astype(np.int64); a1 = a1.astype(np.int64)
    p = np.zeros(a0.shape + (8,), np.int64)
    p[:, 0], p[:, 1] = a0, a1
    m8 = a0 > a1
    for k in range(2, 8):
        v8 = ((8 - k) * a0 + (k - 1) * a1 + 3) // 7
        v6 = ((6 - k) * a0 + (k - 1) * a1 + 2) // 5 if k < 6 else (0 if k == 6 else 255)
        p[:, k] = np.where(m8, v8, v6)
    return p


W4 = np.array([0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64])

# ---------------------------------------------------------------------------------------------------------------
# BC1
# ---------------------------------------------------------------------------------------------------------------

def principal_axis(X, w):
    """X (N,16,C) float, w (N,16) weights -> mean (N,C), unit axis (N,C)"""
    sw = np.maximum(w.sum(1, keepdims=True), 1e-9)
    m = (X * w[..., None]).sum(1) / sw
    D = (X - m[:, None]) * np.sqrt(w)[..., None]
    C = np.einsum('nki,nkj->nij', D, D)
    v = np.ones(m.shape) / np.sqrt(m.shape[1])
    # start along the widest channel, then power iterations
    v = np.where(np.diagonal(C, axis1=1, axis2=2).max(1, keepdims=True) > 0, np.eye(m.shape[1])[np.argmax(np.diagonal(C, axis1=1, axis2=2), 1)] + 0.25 * v, v)
    for _ in range(8):
        v = np.einsum('nij,nj->ni', C, v)
        n = np.linalg.norm(v, axis=1, keepdims=True)
        v = np.where(n > 1e-12, v / np.maximum(n, 1e-12), 1.0 / np.sqrt(m.shape[1]))
    return m, v


def quant565(e):
    q = np.stack([np.round(e[:, 0] * 31 / 255), np.round(e[:, 1] * 63 / 255), np.round(e[:, 2] * 31 / 255)], -1)
    return np.clip(q, 0, [31, 63, 31]).astype(np.int64)


def bc1_assign(X, w, c0, c1, four, transparent=None):
    """nearest palette entries -> indices (N,16), weighted squared error (N,)"""
    p = bc1_palette(c0, c1, four)[:, :, :3].astype(np.float64)
    d = ((X[:, :, None, :] - p[:, None, :, :]) ** 2).sum(-1)       # N,16,4
    if four is None:
        three = ~(c0 > c1)
        d[:, :, 3] = np.where(three[:, None], np.inf, d[:, :, 3])   # index 3 = transparent in 3-colour blocks
    idx = d.argmin(-1)
    err = (np.take_along_axis(d, idx[..., None], -1)[..., 0] * w).sum(1)
    if transparent is not None:
        idx = np.where(transparent, 3, idx)
    return idx, err


def lsq_endpoints(X, w, a):
    """least squares e0, e1 for x ~ a e0 + (1 - a) e1 (weights w) -> (N,C), (N,C), ok"""
    b = 1.0 - a
    aa, ab, bb = (w * a * a).sum(1), (w * a * b).sum(1), (w * b * b).sum(1)
    ax, bx = (w[..., None] * a[..., None] * X).sum(1), (w[..., None] * b[..., None] * X).sum(1)
    det = aa * bb - ab * ab
    ok = np.abs(det) > 1e-9
    sd = np.where(ok, det, 1.0)
    e0 = (bb[:, None] * ax - ab[:, None] * bx) / sd[:, None]
    e1 = (aa[:, None] * bx - ab[:, None] * ax) / sd[:, None]
    return np.clip(e0, 0, 255), np.clip(e1, 0, 255), ok


def encode_bc1_colour(X, w, mode4, naive=False, transparent=None):
    """X (N,16,3) float, w weights; mode4: 4-colour blocks (c0 > c1) else 3-colour (c0 <= c1). -> c0, c1, idx"""
    N = X.shape[0]
    if naive:
        big = np.where(w[..., None] > 0, X, -1).max(1); small = np.where(w[..., None] > 0, X, 256).min(1)
        big = np.where(big < 0, 0, big); small = np.where(small > 255, 0, small)
        e0, e1 = big, small
    else:
        m, v = principal_axis(X, w)
        t = ((X - m[:, None]) * v[:, None]).sum(-1)
        tmax, tmin = np.where(w > 0, t, -np.inf).max(1), np.where(w > 0, t, np.inf).min(1)
        tmax, tmin = np.where(np.isfinite(tmax), tmax, 0), np.where(np.isfinite(tmin), tmin, 0)
        e0, e1 = np.clip(m + tmax[:, None] * v, 0, 255), np.clip(m + tmin[:, None] * v, 0, 255)

    def order(q0, q1):
        c0, c1 = pack565(q0), pack565(q1)
        swap = np.where(mode4, c0 < c1, c0 > c1)
        return np.where(swap, c1, c0), np.where(swap, c0, c1)

    def evaluate(q0, q1):
        c0, c1 = order(q0, q1)
        # 4-colour blocks whose endpoints quantise equal would decode as 3-colour: fine (index 3 is excluded)
        idx, err = bc1_assign(X, w, c0, c1, None, transparent)
        return c0, c1, idx, err

    q0, q1 = quant565(e0), quant565(e1)
    best = evaluate(q0, q1); bq0, bq1 = q0, q1
    if naive: return best[0], best[1], best[2]
    alphas4 = np.array([1.0, 0.0, 2 / 3, 1 / 3]); alphas3 = np.array([1.0, 0.0, 0.5, 0.0])
    for _ in range(3):                                  # least-squares refits for the chosen indices
        c0, c1, idx, err = best
        al = np.where((c0 > c1)[:, None], alphas4[idx], alphas3[idx])
        ww = np.where(idx == 3, np.where((c0 > c1)[:, None], w, 0), w)
        f0, f1, ok = lsq_endpoints(X, ww, al)
        e0n, e1n = unpack565(c0).astype(np.float64), unpack565(c1).astype(np.float64)
        f0 = np.where(ok[:, None], f0, e0n); f1 = np.where(ok[:, None], f1, e1n)
        n0, n1 = quant565(f0), quant565(f1)
        cand = evaluate(n0, n1)
        better = cand[3] < best[3]
        best = tuple(np.where(better[:, None] if a.ndim == 2 else better, x, a) for x, a in zip(cand, best))
        bq0 = np.where(better[:, None], n0, bq0); bq1 = np.where(better[:, None], n1, bq1)
    lim = np.array([31, 63, 31])
    for _ in range(2):                                  # +-1 search on each quantised endpoint component
        for e in range(2):
            for ch in range(3):
                for dlt in (-1, 1):
                    n0, n1 = bq0.copy(), bq1.copy()
                    tgt = n0 if e == 0 else n1
                    tgt[:, ch] = np.clip(tgt[:, ch] + dlt, 0, lim[ch])
                    cand = evaluate(n0, n1)
                    better = cand[3] < best[3]
                    if not better.any(): continue
                    best = tuple(np.where(better[:, None] if a.ndim == 2 else better, x, a) for x, a in zip(cand, best))
                    bq0 = np.where(better[:, None], n0, bq0); bq1 = np.where(better[:, None], n1, bq1)
    return best[0], best[1], best[2]


def pack_bc1(c0, c1, idx):
    bits = (idx.astype(np.uint64) << (2 * np.arange(16, dtype=np.uint64))).sum(1)
    out = np.zeros((len(c0), 8), np.uint8)
    out[:, 0], out[:, 1] = c0 & 255, c0 >> 8
    out[:, 2], out[:, 3] = c1 & 255, c1 >> 8
    for k in range(4): out[:, 4 + k] = (bits >> np.uint64(8 * k)) & np.uint64(255)
    return out


def encode_bc1(blocks, naive=False, four_only=False):
    """blocks (N,16,4) uint8 -> (N,8). Alpha < 128 texels become transparent (3-colour blocks) unless four_only (BC3)."""
    X = blocks[..., :3].astype(np.float64)
    if four_only:
        w = np.ones(X.shape[:2]); trans = None; mode4 = np.ones(len(X), bool)
    else:
        trans = blocks[..., 3] < 128
        w = (~trans).astype(np.float64); mode4 = ~trans.any(1)
    c0, c1, idx = encode_bc1_colour(X, w, mode4, naive, trans)
    if four_only:   # BC3 colour is always 4-colour: keep c0 > c1 (equal endpoints: every index 0 is the same colour)
        eq = c0 == c1
        idx = np.where(eq[:, None], 0, idx)
    return pack_bc1(c0, c1, idx)

# ---------------------------------------------------------------------------------------------------------------
# BC4 (BC3 alpha, BC5 channels)
# ---------------------------------------------------------------------------------------------------------------

def bc4_eval(V, a0, a1):
    p = bc4_palette(a0, a1).astype(np.float64)
    d = (V[:, :, None] - p[:, None, :]) ** 2
    idx = d.argmin(-1)
    return idx, np.take_along_axis(d, idx[..., None], -1)[..., 0].sum(1)


def encode_bc4(V, naive=False):
    """V (N,16) uint8 -> (N,8)"""
    V = V.astype(np.float64)
    hi, lo = V.max(1), V.min(1)
    # 8-level mode: a0 > a1
    a0 = np.round(hi).astype(np.int64); a1 = np.round(lo).astype(np.int64)
    a1 = np.where(a0 == a1, np.maximum(a0 - 1, 0), a1); a0 = np.where(a0 == a1, a0 + 1, a0)
    best_a0, best_a1 = a0, a1
    idx, err = bc4_eval(V, a0, a1)
    if not naive:
        wt8 = np.array([1.0, 0.0] + [(8 - k) / 7 for k in range(2, 8)])
        for _ in range(2):
            al = wt8[idx]
            f0, f1, ok = lsq_endpoints(V[..., None], np.ones(V.shape), al)
            n0 = np.where(ok, np.round(f0[:, 0]), best_a0).astype(np.int64); n1 = np.where(ok, np.round(f1[:, 0]), best_a1).astype(np.int64)
            n0, n1 = np.maximum(n0, n1), np.minimum(n0, n1)
            n1 = np.where(n0 == n1, np.maximum(n0 - 1, 0), n1); n0 = np.where(n0 == n1, n0 + 1, n0)
            i2, e2 = bc4_eval(V, n0, n1); b = e2 < err
            best_a0, best_a1, idx, err = np.where(b, n0, best_a0), np.where(b, n1, best_a1), np.where(b[:, None], i2, idx), np.where(b, e2, err)
        for _ in range(2):
            for which in (0, 1):
                for dlt in (-1, 1):
                    n0, n1 = best_a0.copy(), best_a1.copy()
                    if which == 0: n0 = np.clip(n0 + dlt, 0, 255)
                    else: n1 = np.clip(n1 + dlt, 0, 255)
                    valid = n0 > n1
                    i2, e2 = bc4_eval(V, n0, n1); b = (e2 < err) & valid
                    best_a0, best_a1, idx, err = np.where(b, n0, best_a0), np.where(b, n1, best_a1), np.where(b[:, None], i2, idx), np.where(b, e2, err)
        # 6-level mode (a0 <= a1, plus exact 0 and 255): endpoints span the values strictly between 0 and 255
        inner = (V > 0) & (V < 255)
        ilo = np.where(inner.any(1), np.where(inner, V, 256).min(1), 0); ihi = np.where(inner.any(1), np.where(inner, V, -1).max(1), 0)
        m0, m1 = np.round(ilo).astype(np.int64), np.round(ihi).astype(np.int64)
        i6, e6 = bc4_eval(V, m0, m1)
        for _ in range(2):
            for which in (0, 1):
                for dlt in (-1, 1):
                    n0, n1 = m0.copy(), m1.copy()
                    if which == 0: n0 = np.clip(n0 + dlt, 0, 255)
                    else: n1 = np.clip(n1 + dlt, 0, 255)
                    valid = n0 <= n1
                    i2, e2 = bc4_eval(V, n0, n1); b = (e2 < e6) & valid
                    m0, m1, i6, e6 = np.where(b, n0, m0), np.where(b, n1, m1), np.where(b[:, None], i2, i6), np.where(b, e2, e6)
        b = e6 < err
        best_a0, best_a1, idx = np.where(b, m0, best_a0), np.where(b, m1, best_a1), np.where(b[:, None], i6, idx)
    out = np.zeros((len(V), 8), np.uint8)
    out[:, 0], out[:, 1] = best_a0, best_a1
    bits = (idx.astype(np.uint64) << (3 * np.arange(16, dtype=np.uint64))).sum(1)
    for k in range(6): out[:, 2 + k] = (bits >> np.uint64(8 * k)) & np.uint64(255)
    return out

# ---------------------------------------------------------------------------------------------------------------
# BC7 (mode 6)
# ---------------------------------------------------------------------------------------------------------------

def bc7_quant(e):
    """(N,4) float endpoint -> (N,4) 7-bit values, (N,) p-bit, (N,4) decoded 8-bit"""
    best = None
    for p in (0, 1):
        c = np.clip(np.round((e - p) / 2.0), 0, 127).astype(np.int64)
        v = c * 2 + p
        err = ((v - e) ** 2).sum(1)
        if best is None: best = (c, np.full(len(e), p), v, err)
        else:
            b = err < best[3]
            best = (np.where(b[:, None], c, best[0]), np.where(b, p, best[1]), np.where(b[:, None], v, best[2]), np.where(b, err, best[3]))
    return best[0], best[1], best[2]


def bc7_eval(X, v0, v1):
    pal = ((64 - W4)[None, :, None] * v0[:, None, :] + W4[None, :, None] * v1[:, None, :] + 32) >> 6     # N,16,4
    d = ((X[:, :, None, :] - pal[:, None, :, :].astype(np.float64)) ** 2).sum(-1)
    idx = d.argmin(-1)
    return idx, np.take_along_axis(d, idx[..., None], -1)[..., 0].sum(1)


def encode_bc7(blocks, naive=False):
    X = blocks.astype(np.float64)
    w = np.ones(X.shape[:2])
    if naive:
        e0, e1 = X.min(1), X.max(1)
    else:
        m, v = principal_axis(X, w)
        t = ((X - m[:, None]) * v[:, None]).sum(-1)
        e0, e1 = np.clip(m + t.min(1)[:, None] * v, 0, 255), np.clip(m + t.max(1)[:, None] * v, 0, 255)
    c0, p0, v0 = bc7_quant(e0); c1, p1, v1 = bc7_quant(e1)
    idx, err = bc7_eval(X, v0, v1)
    if not naive:
        wt = 1.0 - W4 / 64.0
        for _ in range(3):
            f0, f1, ok = lsq_endpoints(X, w, wt[idx])
            f0 = np.where(ok[:, None], f0, v0); f1 = np.where(ok[:, None], f1, v1)
            n0, q0, nv0 = bc7_quant(f0); n1, q1, nv1 = bc7_quant(f1)
            i2, e2 = bc7_eval(X, nv0, nv1); b = e2 < err
            c0, p0, v0 = np.where(b[:, None], n0, c0), np.where(b, q0, p0), np.where(b[:, None], nv0, v0)
            c1, p1, v1 = np.where(b[:, None], n1, c1), np.where(b, q1, p1), np.where(b[:, None], nv1, v1)
            idx, err = np.where(b[:, None], i2, idx), np.where(b, e2, err)
    # the anchor (texel 0) has a 3-bit index: swap the endpoints when it needs the top half
    sw = idx[:, 0] >= 8
    c0, c1 = np.where(sw[:, None], c1, c0), np.where(sw[:, None], c0, c1)
    p0, p1 = np.where(sw, p1, p0), np.where(sw, p0, p1)
    idx = np.where(sw[:, None], 15 - idx, idx)
    # pack: mode 6 (bit 6), R0 R1 G0 G1 B0 B1 A0 A1 (7 bits), P0 P1, indices (texel 0: 3 bits, others 4)
    out = np.zeros((len(X), 16), np.uint8)
    fields = []
    for ch in range(4): fields += [(c0[:, ch], 7), (c1[:, ch], 7)]
    fields += [(p0, 1), (p1, 1)]
    fields += [(idx[:, 0], 3)] + [(idx[:, i], 4) for i in range(1, 16)]
    lo = np.zeros(len(X), np.uint64); hi = np.zeros(len(X), np.uint64); pos = 0
    for val, nb in [(np.full(len(X), 1 << 6), 7)] + fields:
        val = np.asarray(val).astype(np.uint64)
        for b in range(nb):
            bit = (val >> np.uint64(b)) & np.uint64(1)
            if pos < 64: lo |= bit << np.uint64(pos)
            else: hi |= bit << np.uint64(pos - 64)
            pos += 1
    assert pos == 128
    for k in range(8):
        out[:, k] = (lo >> np.uint64(8 * k)) & np.uint64(255)
        out[:, 8 + k] = (hi >> np.uint64(8 * k)) & np.uint64(255)
    return out

# ---------------------------------------------------------------------------------------------------------------
# decoding (as src/arc_texc.c), for quality numbers and checks
# ---------------------------------------------------------------------------------------------------------------

def decode_bc1_blocks(b, four=False):
    c0 = b[:, 0].astype(np.int64) | (b[:, 1].astype(np.int64) << 8); c1 = b[:, 2].astype(np.int64) | (b[:, 3].astype(np.int64) << 8)
    bits = sum(b[:, 4 + k].astype(np.int64) << (8 * k) for k in range(4))
    idx = (bits[:, None] >> (2 * np.arange(16))) & 3
    p = bc1_palette(c0, c1, True if four else None)
    return np.take_along_axis(p, idx[..., None].repeat(4, -1), 1)


def decode_bc4_blocks(b):
    bits = sum(b[:, 2 + k].astype(np.int64) << (8 * k) for k in range(6))
    idx = (bits[:, None] >> (3 * np.arange(16))) & 7
    return np.take_along_axis(bc4_palette(b[:, 0], b[:, 1]), idx, 1)


def decode_bc7_mode6(b):
    lo = sum(b[:, k].astype(np.uint64) << np.uint64(8 * k) for k in range(8)); hi = sum(b[:, 8 + k].astype(np.uint64) << np.uint64(8 * k) for k in range(8))
    def get(pos, n):
        v = np.zeros(len(b), np.int64)
        for i in range(n):
            q = pos + i
            bit = ((lo >> np.uint64(q)) if q < 64 else (hi >> np.uint64(q - 64))) & np.uint64(1)
            v |= bit.astype(np.int64) << i
        return v
    assert (b[:, 0] == 0x40).all() or ((b[:, 0] & 0x7F) == 0x40).all(), 'only mode 6 here'
    pos = 7; e = np.zeros((len(b), 2, 4), np.int64)
    for ch in range(4):
        for j in range(2): e[:, j, ch] = get(pos, 7); pos += 7
    p = [get(pos, 1), get(pos + 1, 1)]; pos += 2
    for j in range(2): e[:, j] = e[:, j] * 2 + p[j][:, None]
    idx = np.zeros((len(b), 16), np.int64)
    for i in range(16):
        n = 3 if i == 0 else 4; idx[:, i] = get(pos, n); pos += n
    w = W4[idx]
    return ((64 - w)[..., None] * e[:, None, 0] + w[..., None] * e[:, None, 1] + 32) >> 6


def normal_z(x, y):
    xf = x / 127.5 - 1.0; yf = y / 127.5 - 1.0
    z2 = 1.0 - xf * xf - yf * yf
    z = np.where(z2 > 0, np.sqrt(np.maximum(z2, 0)), 0.0)
    return np.minimum(np.floor(127.5 + 127.5 * z + 0.5), 255).astype(np.int64)


def decode_blocks(fmt, data, w, h):
    bb = BLOCK_BYTES[fmt]
    b = np.frombuffer(data, np.uint8)[: ((w + 3) // 4) * ((h + 3) // 4) * bb].reshape(-1, bb)
    if fmt == 1: px = decode_bc1_blocks(b)
    elif fmt == 3:
        px = decode_bc1_blocks(b[:, 8:], four=True); px[..., 3] = decode_bc4_blocks(b[:, :8])
    elif fmt == 4:
        r = decode_bc4_blocks(b); px = np.zeros(r.shape + (4,), np.int64); px[..., 0] = r; px[..., 3] = 255
    elif fmt == 5:
        x, y = decode_bc4_blocks(b[:, :8]), decode_bc4_blocks(b[:, 8:])
        px = np.stack([x, y, normal_z(x, y), np.full(x.shape, 255)], -1)
    else: px = decode_bc7_mode6(b)
    return from_blocks(px.astype(np.uint8), w, h)

# ---------------------------------------------------------------------------------------------------------------
# images <-> .arct
# ---------------------------------------------------------------------------------------------------------------

def encode_level(img, fmt, naive=False):
    blocks = to_blocks(img)
    if fmt == 1: return encode_bc1(blocks, naive).tobytes()
    if fmt == 3: return np.concatenate([encode_bc4(blocks[..., 3], naive), encode_bc1(blocks, naive, four_only=True)], 1).tobytes()
    if fmt == 4: return encode_bc4(blocks[..., 0], naive).tobytes()
    if fmt == 5: return np.concatenate([encode_bc4(blocks[..., 0], naive), encode_bc4(blocks[..., 1], naive)], 1).tobytes()
    if fmt == 7: return encode_bc7(blocks, naive).tobytes()
    raise ValueError(fmt)


def pick_format(img, fmt='auto', normal=False):
    if fmt != 'auto': return FMT[fmt]
    if normal: return 5
    a = img[..., 3]
    return 1 if (a == 255).all() or ((a == 0) | (a == 255)).all() else 3


def rgba_array(im):
    from PIL import Image
    if isinstance(im, np.ndarray): return im
    return np.asarray(im.convert('RGBA')).copy()


def compress(img, fmt='auto', normal=False, mips=True, use_zlib=True, naive=False):
    """img: PIL image or HxWx4 uint8 -> (.arct bytes, format number)"""
    img = rgba_array(img)
    h, w = img.shape[:2]
    if w % 4 or h % 4 or w < 4 or h < 4: raise ValueError('sides must be multiples of 4 (got %dx%d)' % (w, h))
    f = pick_format(img, fmt, normal)
    if normal and f in (1, 3, 7):   # normals stored as colour: keep them unit length after quantising? (Z kept as is)
        pass
    levels = mip_chain(img, normal, mips)
    payload = b''.join(encode_level(l, f, naive) for l in levels)
    flags = 1 if normal else 0
    if use_zlib:
        z = zlib.compress(payload, 9)
        if len(z) <= len(payload) * 0.95: payload, flags = z, flags | 2
    return b'ARCT' + struct.pack('<7I', 1, f, w, h, len(levels), flags, len(payload)) + payload, f


def read_arct(data):
    """-> dict(fmt, w, h, levels=[RGBA arrays], flags)"""
    assert data[:4] == b'ARCT', 'not an .arct'
    ver, fmt, w, h, nl, flags, stored = struct.unpack('<7I', data[4:32])
    payload = data[32:32 + stored]
    if flags & 2: payload = zlib.decompress(payload)
    out, at, lw, lh = [], 0, w, h
    for _ in range(nl):
        n = ((lw + 3) // 4) * ((lh + 3) // 4) * BLOCK_BYTES[fmt]
        out.append(decode_blocks(fmt, payload[at:at + n], lw, lh)); at += n
        lw, lh = max(lw // 2, 1), max(lh // 2, 1)
    return dict(fmt=fmt, w=w, h=h, levels=out, flags=flags)


def psnr(a, b, channels=3):
    d = a[..., :channels].astype(np.float64) - b[..., :channels].astype(np.float64)
    mse = (d * d).mean()
    return float('inf') if mse == 0 else 10 * np.log10(255.0 ** 2 / mse)


def normal_error_deg(a, b):
    """mean angle (degrees) between normal maps a and b (X, Y; Z rebuilt for both)"""
    def vec(x):
        v = x[..., :2].astype(np.float64) / 127.5 - 1.0
        z = np.sqrt(np.maximum(1 - (v ** 2).sum(-1), 0))
        n = np.concatenate([v, z[..., None]], -1)
        return n / np.maximum(np.linalg.norm(n, axis=-1, keepdims=True), 1e-9)
    c = np.clip((vec(a) * vec(b)).sum(-1), -1, 1)
    return float(np.degrees(np.arccos(c)).mean())

# ---------------------------------------------------------------------------------------------------------------
# .arcm meshes: recompress the texture and material maps (version 4)
# ---------------------------------------------------------------------------------------------------------------

def decode_image(b):
    from PIL import Image
    if b[:4] == b'ARCT': return None
    return np.asarray(Image.open(io.BytesIO(b)).convert('RGBA')).copy()


def shrink(img, n):
    if not n or max(img.shape[:2]) <= n: return img
    from PIL import Image
    k = n / max(img.shape[:2]); im = Image.fromarray(img)
    return np.asarray(im.resize((max(4, round(img.shape[1] * k) // 4 * 4), max(4, round(img.shape[0] * k) // 4 * 4)), Image.LANCZOS)).copy()


def recompress_arcm(src, dst, a, log=print):
    d = open(src, 'rb').read()
    if d[:4] != b'ARCM': raise ValueError('%s is not an .arcm' % src)
    ver, vc, ic, flags, tex_bytes, mat_bytes = struct.unpack('<6I', d[4:28])
    if ver not in (2, 3, 4):
        open(dst, 'wb').write(d); return dict(changed=False)
    at = 56 if ver >= 3 else 32
    at += vc * (16 if ver >= 3 else 36) + (vc * 8 if flags & 1 else 0)
    ib = ic * (2 if flags & 4 else 4); at += (ib + 3) & ~3
    head, tex, rest = d[:at], d[at:at + tex_bytes], d[at + tex_bytes:at + tex_bytes + mat_bytes]
    tail = d[at + tex_bytes + mat_bytes:]   # 0.10.15: a GLOW block (the glow mask) is kept as it is: a mask must stay exact
    stats = dict(changed=False, before=len(d), images=[])

    def conv(b, kind):
        if not b: return b
        img = decode_image(b)
        if img is None: return b                         # already compressed
        img = shrink(img, a.max_size)
        h, w = img.shape[:2]
        fmtname = {'color': a.color_format, 'normal': a.normal_format, 'map': a.map_format}[kind]
        if fmtname == 'keep' or w % 4 or h % 4 or w < 4 or h < 4: return b
        out, f = compress(img, fmtname, normal=(kind == 'normal' and fmtname == 'bc5'), mips=not a.no_mips, use_zlib=not a.no_zlib, naive=a.naive)
        lv = read_arct(out)['levels'][0]
        q = normal_error_deg(img, lv) if kind == 'normal' and f == 5 else psnr(img, lv, 4 if f in (3, 7) else 3)
        nl = struct.unpack('<I', out[20:24])[0]
        gpu_after = sum(((max(w >> i, 1) + 3) // 4) * ((max(h >> i, 1) + 3) // 4) * BLOCK_BYTES[f] for i in range(nl))
        stats['images'].append(dict(kind=kind, fmt=FMT_NAME[f], w=w, h=h, before=len(b), after=len(out), quality=q,
                                    gpu_before=w * h * 4 * 4 // 3, gpu_after=gpu_after))
        stats['changed'] = True
        return out

    tex2 = conv(tex, 'color')
    mat2 = rest
    if mat_bytes >= 52 and rest[:4] == b'MATL':
        sz = struct.unpack('<3I', rest[40:52]); off = 52; maps = []
        for i in range(3):
            maps.append(rest[off:off + sz[i]]); off += sz[i]
        maps2 = [conv(maps[0], 'normal'), conv(maps[1], 'map'), conv(maps[2], 'map')]
        mat2 = rest[:40] + struct.pack('<3I', *[len(m) for m in maps2]) + b''.join(maps2)
    # version 4 = the compact (v3) layout with .arct images; a version 2 file (36-byte vertices) keeps its number and layout
    # (ARC 0.7.1 reads .arct images in any version; an older ARC would load it without its texture)
    newver = 4 if stats['changed'] and ver >= 3 else ver
    out = head[:4] + struct.pack('<6I', newver, vc, ic, flags, len(tex2), len(mat2)) + head[28:] + tex2 + mat2 + tail
    open(dst, 'wb').write(out)
    stats['after'] = len(out)
    return stats


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('src', nargs='?'); ap.add_argument('dst', nargs='?')
    ap.add_argument('--format', default='auto', choices=['auto', 'bc1', 'bc3', 'bc5', 'bc7'])
    ap.add_argument('--normal', action='store_true'); ap.add_argument('--no-mips', action='store_true')
    ap.add_argument('--no-zlib', action='store_true'); ap.add_argument('--naive', action='store_true')
    ap.add_argument('--arcm', nargs=2, metavar=('IN', 'OUT')); ap.add_argument('--arcm-batch', nargs=2, metavar=('SRC_DIR', 'DST_DIR'))
    ap.add_argument('--color-format', default='auto', choices=['auto', 'bc1', 'bc3', 'bc7', 'keep'])
    ap.add_argument('--normal-format', default='bc5', choices=['bc5', 'bc3', 'bc1', 'bc7', 'keep'])
    ap.add_argument('--map-format', default='bc1', choices=['bc1', 'bc3', 'bc7', 'keep'])
    ap.add_argument('--max-size', type=int, default=0)
    ap.add_argument('--report', metavar='IMAGE')
    a = ap.parse_args()
    if a.report:
        from PIL import Image
        img = rgba_array(Image.open(a.report)); h, w = img.shape[:2]
        for naive in (False, True):
            out, f = compress(img, a.format, a.normal, mips=False, use_zlib=False, naive=naive)
            lv = read_arct(out)['levels'][0]
            q = ('%.2f deg' % normal_error_deg(img, lv)) if f == 5 else ('%.2f dB' % psnr(img, lv, 4 if f in (3, 7) else 3))
            print('%-6s %-4s %dx%d  %s' % ('naive' if naive else 'arc', FMT_NAME[f], w, h, q))
        return
    if a.arcm:
        st = recompress_arcm(a.arcm[0], a.arcm[1], a)
        for im in st.get('images', []): print('  %-6s %s %dx%d  %d -> %d bytes  quality %.2f' % (im['kind'], im['fmt'], im['w'], im['h'], im['before'], im['after'], im['quality']))
        print('%s: %d -> %d bytes' % (a.arcm[1], st.get('before', 0), st.get('after', 0)))
        return
    if a.arcm_batch:
        sd, dd = a.arcm_batch; os.makedirs(dd, exist_ok=True)
        for f in sorted(os.listdir(sd)):
            s, t = os.path.join(sd, f), os.path.join(dd, f)
            if os.path.isdir(s): continue
            if f.lower().endswith('.arcm'):
                st = recompress_arcm(s, t, a); print('%s: %d -> %d bytes' % (f, st.get('before', 0), st.get('after', 0)))
            else: open(t, 'wb').write(open(s, 'rb').read())
        return
    if not a.src or not a.dst: ap.error('give IN and OUT (or --arcm / --arcm-batch / --report)')
    from PIL import Image
    out, f = compress(Image.open(a.src), a.format, a.normal, not a.no_mips, not a.no_zlib, a.naive)
    open(a.dst, 'wb').write(out)
    print('%s: %s, %d bytes' % (a.dst, FMT_NAME[f], len(out)))


if __name__ == '__main__':
    main()
