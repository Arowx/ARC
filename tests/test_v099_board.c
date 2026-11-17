/* ARC 0.9.9 self-test: arc_board (MechBattleArc's grid tactics request). Included by test_core.c; v099b_init() runs in
   init. Names start with v099b_ (one translation unit). */
static arc_board v099b_b; static int16_t v099b_cost[ARC_BOARD_MAX * ARC_BOARD_MAX];
static void v099b_random(arc_board *b, arc_rng *r, int w, int h) {
    arc_board_init(b, w, h, ARC_BOARD_CHEBYSHEV);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        int k = arc_rng_range(r, 0, 99);
        if (k < 14) arc_board_set_tile(b, x, y, 0, ARC_COVER_FULL);          /* wall */
        else if (k < 26) arc_board_set_tile(b, x, y, 2, ARC_COVER_HALF);     /* forest */
        else if (k < 32) arc_board_set_tile(b, x, y, 3, ARC_COVER_NONE);     /* water */
        if (k >= 95) arc_board_set_occupant(b, x, y, 1 + k);
    }
}
static void v099b_los(void) {
    arc_board *b = &v099b_b; int cv;
    arc_board_init(b, 10, 10, ARC_BOARD_CHEBYSHEV);
    CHECK(arc_board_los(b, 0, 0, 9, 9, &cv) && cv == 0);
    arc_board_set_tile(b, 4, 0, 0, ARC_COVER_FULL);
    CHECK(!arc_board_los(b, 0, 0, 9, 0, NULL));                     /* a wall in the way */
    CHECK(arc_board_los(b, 0, 0, 4, 0, &cv));                        /* the end tiles don't count */
    arc_board_set_tile(b, 2, 1, 2, ARC_COVER_HALF);
    CHECK(arc_board_los(b, 0, 1, 5, 1, &cv) && cv == ARC_COVER_HALF);
    /* exactly through a corner: (0,0) -> (2,2) passes the corner shared by (1,0) and (0,1) ... */
    arc_board_init(b, 4, 4, ARC_BOARD_CHEBYSHEV);
    arc_board_set_tile(b, 1, 0, 0, ARC_COVER_FULL);
    CHECK(arc_board_los(b, 0, 0, 1, 1, &cv) && cv == ARC_COVER_HALF);   /* one full tile at the corner: half cover */
    arc_board_set_tile(b, 0, 1, 0, ARC_COVER_FULL);
    CHECK(!arc_board_los(b, 0, 0, 1, 1, NULL));                         /* both: blocked */
    CHECK(!arc_board_los(b, 1, 1, 0, 0, NULL));
    /* units: give cover only when the board says so */
    arc_board_init(b, 6, 1, ARC_BOARD_CHEBYSHEV); arc_board_set_occupant(b, 2, 0, 7);
    CHECK(arc_board_los(b, 0, 0, 5, 0, &cv) && cv == 0);
    b->unit_cover = ARC_COVER_HALF; CHECK(arc_board_los(b, 0, 0, 5, 0, &cv) && cv == ARC_COVER_HALF);
    b->unit_cover = ARC_COVER_FULL; CHECK(!arc_board_los(b, 0, 0, 5, 0, NULL));
    /* symmetric on 10 000 random pairs over random boards: the same answer and the same cover both ways */
    arc_rng r; arc_rng_seed(&r, 99, 1); int bad = 0, seen = 0, half = 0;
    for (int t = 0; t < 10000; t++) {
        if (t % 500 == 0) { v099b_random(b, &r, 40, 30); b->unit_cover = (t / 500) % 3; }
        int x0 = arc_rng_range(&r, 0, 39); int y0 = arc_rng_range(&r, 0, 29); int x1 = arc_rng_range(&r, 0, 39); int y1 = arc_rng_range(&r, 0, 29);
        int c1 = -1, c2 = -2; bool a = arc_board_los(b, x0, y0, x1, y1, &c1), c = arc_board_los(b, x1, y1, x0, y0, &c2);
        if (a != c || (a && c1 != c2)) bad++;
        seen += a; half += a && c1 == ARC_COVER_HALF;
    }
    printf("ARC note board LOS: 10000 random pairs, %d asymmetric, %d visible (%d through half cover)\n", bad, seen, half);
    CHECK(bad == 0 && seen > 1000 && half > 100);
}
/* the slow, obvious answer: relax every edge until nothing changes */
static int v099b_slow[ARC_BOARD_MAX * ARC_BOARD_MAX];
static void v099b_brute(const arc_board *b, int sx, int sy, int budget, int flags) {
    int n = b->w * b->h; for (int i = 0; i < n; i++) v099b_slow[i] = -1;
    v099b_slow[sy * b->w + sx] = 0;
    for (bool changed = true; changed;) {
        changed = false;
        for (int y = 0; y < b->h; y++) for (int x = 0; x < b->w; x++) {
            int c0 = v099b_slow[y * b->w + x]; if (c0 < 0) continue;
            for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
                if ((!dx && !dy) || (dx && dy && !(flags & ARC_BOARD_DIAGONAL))) continue;
                int nx = x + dx, ny = y + dy; if (nx < 0 || ny < 0 || nx >= b->w || ny >= b->h) continue;
                int c = arc_board_cost(b, nx, ny); if (!c) continue;
                if (arc_board_occupant(b, nx, ny) && !(flags & ARC_BOARD_IGNORE_UNITS)) continue;
                if (dx && dy && (!arc_board_cost(b, x + dx, y) || !arc_board_cost(b, x, y + dy))) continue;
                int nc = c0 + c; if (nc > budget) continue;
                int *d = &v099b_slow[ny * b->w + nx]; if (*d < 0 || nc < *d) { *d = nc; changed = true; }
            }
        }
    }
}
static void v099b_reach_paths(void) {
    arc_board *b = &v099b_b; arc_rng r; arc_rng_seed(&r, 7, 2);
    int mismatch = 0, path_bad = 0, corner_cut = 0, paths = 0;
    for (int t = 0; t < 200; t++) {
        int w = arc_rng_range(&r, 5, 30); int h = arc_rng_range(&r, 5, 30);
        v099b_random(b, &r, w, h);
        int sx = arc_rng_range(&r, 0, w - 1); int sy = arc_rng_range(&r, 0, h - 1);
        arc_board_set_tile(b, sx, sy, 1, 0); arc_board_set_occupant(b, sx, sy, 0);
        int flags = (t & 1 ? ARC_BOARD_DIAGONAL : 0) | (t % 4 == 3 ? ARC_BOARD_IGNORE_UNITS : 0), budget = arc_rng_range(&r, 3, 40);
        arc_board_reach(b, sx, sy, budget, flags, v099b_cost); v099b_brute(b, sx, sy, budget, flags);
        for (int i = 0; i < w * h; i++) mismatch += v099b_cost[i] != v099b_slow[i];
        /* paths: the cheapest cost (an unlimited reach agrees), each step a legal move, no blocked corner cut */
        arc_board_reach(b, sx, sy, 100000, flags, v099b_cost);
        for (int k = 0; k < 5; k++) {
            int tx = arc_rng_range(&r, 0, w - 1); int ty = arc_rng_range(&r, 0, h - 1);
            int xy[2 * ARC_BOARD_MAX * 4], total = -1, n = arc_board_path(b, sx, sy, tx, ty, flags, xy, ARC_BOARD_MAX * 4, &total);
            int want = v099b_cost[ty * w + tx];
            if ((n < 0) != (want < 0) || (n >= 0 && total != want)) { path_bad++; continue; }
            if (n <= 0) continue;
            paths++;
            int px = sx, py = sy, sum = 0;
            for (int s = 0; s < n; s++) {
                int nx = xy[2 * s], ny = xy[2 * s + 1], dx = nx - px, dy = ny - py;
                if (abs(dx) > 1 || abs(dy) > 1 || (dx && dy && !(flags & ARC_BOARD_DIAGONAL)) || !arc_board_cost(b, nx, ny)) path_bad++;
                if (dx && dy && (!arc_board_cost(b, px + dx, py) || !arc_board_cost(b, px, py + dy))) corner_cut++;
                sum += arc_board_cost(b, nx, ny); px = nx; py = ny;
            }
            if (sum != total || px != tx || py != ty) path_bad++;
        }
    }
    printf("ARC note board reach: 200 random boards against a brute-force search, %d tiles differ; %d paths, %d wrong, %d cut a corner\n", mismatch, paths, path_bad, corner_cut);
    CHECK(mismatch == 0 && path_bad == 0 && corner_cut == 0 && paths > 300);
    /* ONTO_TARGET: a path may end on an occupied tile (walk up to an enemy), never pass through one */
    arc_board_init(b, 5, 1, ARC_BOARD_CHEBYSHEV); arc_board_set_occupant(b, 4, 0, 3);
    CHECK(arc_board_path(b, 0, 0, 4, 0, 0, NULL, 0, NULL) == -1 && arc_board_path(b, 0, 0, 4, 0, ARC_BOARD_ONTO_TARGET, NULL, 0, NULL) == 4);
    arc_board_set_occupant(b, 2, 0, 9);
    CHECK(arc_board_path(b, 0, 0, 4, 0, ARC_BOARD_ONTO_TARGET, NULL, 0, NULL) == -1);
}
static void v099b_arcs_misc(void) {
    int ok = 0;
    for (int f = 0; f < 8; f++) {
        int dx, dy, lx, ly, rx, ry; arc_board_dir(f, &dx, &dy); arc_board_dir(f - 2, &lx, &ly); arc_board_dir(f + 2, &rx, &ry);
        ok += arc_board_arc(f, 10, 10, 10 + 4 * dx, 10 + 4 * dy) == ARC_ARC_FRONT;
        ok += arc_board_arc(f, 10, 10, 10 - 4 * dx, 10 - 4 * dy) == ARC_ARC_REAR;
        ok += arc_board_arc(f, 10, 10, 10 + 4 * rx, 10 + 4 * ry) == ARC_ARC_RIGHT;
        ok += arc_board_arc(f, 10, 10, 10 + 4 * lx, 10 + 4 * ly) == ARC_ARC_LEFT;
    }
    CHECK(ok == 32);
    CHECK(arc_board_arc(0, 0, 0, 5, 2) == ARC_ARC_FRONT && arc_board_arc(0, 0, 0, 2, 5) == ARC_ARC_RIGHT && arc_board_arc(0, 0, 0, -3, 1) == ARC_ARC_REAR);
    arc_board *b = &v099b_b;
    arc_board_init(b, 8, 6, ARC_BOARD_CHEBYSHEV);
    CHECK(arc_board_range(b, 0, 0, 3, 5) == 5);
    b->metric = ARC_BOARD_MANHATTAN; CHECK(arc_board_range(b, 0, 0, 3, 5) == 8);
    int xy[64];
    CHECK(arc_board_edge_tiles(b, ARC_EDGE_NORTH | ARC_EDGE_SOUTH, false, xy, 32) == 16 && arc_board_edge_tiles(b, 15, false, NULL, 0) == 24);
    arc_board_set_tile(b, 0, 0, 0, ARC_COVER_FULL); arc_board_set_occupant(b, 3, 0, 1);
    CHECK(arc_board_edge_tiles(b, ARC_EDGE_NORTH, true, xy, 32) == 6 && xy[0] == 1 && xy[2] == 2 && xy[4] == 4);
    CHECK(!arc_board_init(b, 0, 5, 0) && !arc_board_init(b, ARC_BOARD_MAX + 1, 5, 0));
    /* the same answers every run */
    uint64_t h[2];
    for (int pass = 0; pass < 2; pass++) {
        arc_rng r; arc_rng_seed(&r, 31, 4); v099b_random(b, &r, 50, 50); uint64_t x = 1469598103934665603ull;
        arc_board_reach(b, 25, 25, 30, ARC_BOARD_DIAGONAL, v099b_cost);
        for (int i = 0; i < 2500; i++) { x ^= (uint16_t)v099b_cost[i]; x *= 1099511628211ull; }
        int path[400]; int n = arc_board_path(b, 0, 0, 49, 49, ARC_BOARD_DIAGONAL, path, 200, NULL);
        for (int i = 0; i < 2 * (n > 0 ? n : 0) && i < 400; i++) { x ^= (uint32_t)path[i]; x *= 1099511628211ull; }
        h[pass] = x;
    }
    CHECK(h[0] == h[1]);
}
static void v099b_images(void) {   /* 0.9.9 arc_image_* (Moonbase Raid's item 4) */
    enum { N = 32 }; static uint8_t a[N * N * 4], b[2 * 2 * 4];
    for (int i = 0; i < N * N; i++) { a[i * 4] = 200; a[i * 4 + 1] = 100; a[i * 4 + 2] = 50; a[i * 4 + 3] = 77; }
    arc_image_multiply(a, N, N, 0x808080, 1.0f);
    CHECK(abs(a[0] - 100) <= 1 && abs(a[1] - 50) <= 1 && abs(a[2] - 25) <= 1 && a[3] == 77);   /* alpha kept */
    arc_image_multiply(a, N, N, 0x000000, 0.0f); CHECK(abs(a[0] - 100) <= 1);                  /* amount 0: unchanged */
    memset(b, 255, sizeof b); arc_image_overlay(a, N, N, b, 2, 2, ARC_IMAGE_NORMAL, 0.5f, false);
    CHECK(abs(a[0] - 178) <= 1 && abs(a[2] - 140) <= 1);                                      /* half way to white */
    for (int i = 0; i < 4; i++) { b[i * 4] = (uint8_t)(i & 1 ? 255 : 0); b[i * 4 + 1] = b[i * 4 + 2] = 128; b[i * 4 + 3] = 255; }
    for (int i = 0; i < N * N; i++) { a[i * 4] = a[i * 4 + 1] = a[i * 4 + 2] = 200; a[i * 4 + 3] = 255; }
    arc_image_overlay(a, N, N, b, 2, 2, ARC_IMAGE_MULTIPLY, 1.0f, true);                       /* repeated 2 x 2: columns alternate */
    CHECK(a[0] == 0 && a[4] == 200 && a[8] == 0 && abs(a[1] - 100) <= 1);
    for (int i = 0; i < N * N; i++) { a[i * 4] = a[i * 4 + 1] = a[i * 4 + 2] = 128; a[i * 4 + 3] = 255; }
    arc_image_noise(a, N, N, 4, 3, 0.5f, 7);
    double seam = 0, inner = 0, mean = 0; int lo = 255, hi = 0;
    for (int y = 0; y < N; y++) {
        seam += abs(a[(y * N + N - 1) * 4] - a[(y * N) * 4]) + abs(a[((N - 1) * N + y) * 4] - a[y * 4]);          /* across the wrap */
        inner += abs(a[(y * N + 10) * 4] - a[(y * N + 11) * 4]) + abs(a[(10 * N + y) * 4] - a[(11 * N + y) * 4]);   /* inside */
        for (int x = 0; x < N; x++) { int v = a[(y * N + x) * 4]; mean += v; if (v < lo) lo = v; if (v > hi) hi = v; }
    }
    mean /= N * N;
    printf("ARC note image noise: range %d..%d, mean %.0f; edge step across the wrap %.2f, inside %.2f\n", lo, hi, mean, seam / (2 * N), inner / (2 * N));
    CHECK(hi - lo > 30 && fabs(mean - 128) < 20 && seam < inner * 2 + 2 * N);                  /* varies; tiles without a seam */
    static uint8_t c1[N * N * 4], c2[N * N * 4]; memset(c1, 160, sizeof c1);
    arc_image_char(c1, N, N, 1.0f, 3); memset(c2, 160, sizeof c2); arc_image_char(c2, N, N, 1.0f, 3);
    double cm = 0; int bright = 0; for (int i = 0; i < N * N; i++) { cm += c1[i * 4]; bright += c1[i * 4] > 60; }
    CHECK(memcmp(c1, c2, sizeof c1) == 0 && cm / (N * N) < 60 && c1[3] == 160);              /* deterministic, dark, alpha kept */
    arc_image_multiply(NULL, N, N, 0, 1); arc_image_overlay(a, 0, N, b, 2, 2, 0, 1, false);   /* bad input: no crash */
    (void)bright;
}
static void v099b_init(void) { v099b_los(); v099b_reach_paths(); v099b_arcs_misc(); v099b_images(); }
