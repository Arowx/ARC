/* ARC 0.8.2 self-test: word-wrapped text and the texture size cap (MechBattleArc's requests). Included by
   test_core.c; v082_init() runs in init. Names start with v082_ (one translation unit). */
static void v082_wrap(void) {
    /* the built-in font is 8 px a character at scale 1; lines are 10 px apart, the last 8 px tall */
    CHECK(arc_text_wrapped_height(80, 1, "aaaa bbbb cccc") == 18.0f);        /* "aaaa bbbb" fits 80, "cccc" wraps */
    CHECK(arc_text_wrapped_height(200, 1, "aaaa bbbb cccc") == 8.0f);        /* one line */
    CHECK(arc_text_wrapped_height(80, 1, "a\n\nb") == 28.0f);                /* '\n' kept, the empty line too */
    CHECK(arc_text_wrapped_height(40, 1, "abcdefghijklmnop") == 38.0f);      /* a long word split, 5 a line */
    CHECK(arc_text_wrapped_height(32, 1, "aaaa      ") == 8.0f);             /* trailing spaces don't wrap */
    CHECK(arc_text_wrapped_height(32, 1, "aaaa      bbbb") == 18.0f);        /* nor start the next line */
    CHECK(arc_text_wrapped_height(0, 1, "aaaa bbbb\ncccc") == 18.0f);        /* width 0: only '\n' breaks */
    CHECK(arc_text_wrapped_height(160, 2, "aaaa bbbb cccc") == 36.0f);       /* scale 2: 16 px a character */
    /* whole UTF-8 characters only: each 2-byte character is 16 px here, so 24 px holds one */
    CHECK(arc_text_wrapped_height(24, 1, "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9") == 38.0f);
    /* drawing returns the same height, with any flags */
    CHECK(arc_draw_text_wrapped(0, 0, 80, 1, 0xFFFFFF, ARC_TEXT_CENTER | ARC_TEXT_SHADOW, "aaaa bbbb cccc") == 18.0f);
    CHECK(arc_draw_text_wrapped(0, 100, 80, 1, 0xFFFFFF, ARC_TEXT_BOTTOM, "aaaa bbbb cccc") == 18.0f);
}
static void v082_cap(void) {
    static uint8_t px[64 * 32 * 4], odd[33 * 17 * 4];
    for (int i = 0; i < 64 * 32; i++) { px[i * 4] = (uint8_t)((i % 64) & 1 ? 255 : 0); px[i * 4 + 1] = 128; px[i * 4 + 2] = 0; px[i * 4 + 3] = 255; }
    for (int i = 0; i < 33 * 17 * 4; i++) odd[i] = 200;
    CHECK(arc_image_save_png("v082_cap.png", px, 64, 32)); CHECK(arc_image_save_png("v082_odd.png", odd, 33, 17));
    int w = 0, h = 0;
    CHECK(arc_texture_max_size() == 0);
    arc_set_texture_max_size(16); CHECK(arc_texture_max_size() == 16);
    arc_texture t = arc_texture_load_ex("v082_cap.png", ARC_TEX_MESH); arc_texture_size(t, &w, &h); CHECK(w == 16 && h == 8);   /* halved twice, shape kept */
    arc_texture u = arc_texture_load_ex("v082_cap.png", ARC_TEX_SMOOTH); arc_texture_size(u, &w, &h); CHECK(w == 64 && h == 32);   /* 2D: never capped */
    arc_texture o = arc_texture_load_ex("v082_odd.png", ARC_TEX_MIPMAPS); arc_texture_size(o, &w, &h); CHECK(w == 9 && h == 5);    /* 33x17 -> 17x9 -> 9x5 */
    arc_set_texture_max_size(0);
    arc_texture f = arc_texture_load_ex("v082_cap.png", ARC_TEX_MESH); arc_texture_size(f, &w, &h); CHECK(w == 64 && h == 32);   /* no cap */
    arc_set_texture_max_size(1); CHECK(arc_texture_max_size() == 4); arc_set_texture_max_size(-5); CHECK(arc_texture_max_size() == 0);
    arc_texture_destroy(t); arc_texture_destroy(u); arc_texture_destroy(o); arc_texture_destroy(f);
    remove("v082_cap.png"); remove("v082_odd.png");
}
/* 0.8.3 faster loading: the integer JPEG decoder, reduced-size JPEG decoding under the cap, and the table-driven
   inflate (through PNG). A smooth picture written by ARC's own JPEG writer must come back close; a capped load of it
   must have the size that decoding in full and halving gives; a PNG must come back exactly. */
static void v083_loading(void) {
    enum { W = 200, H = 120 };
    static uint8_t px[W * H * 4];
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        uint8_t *p = px + (y * W + x) * 4; p[0] = (uint8_t)(x * 255 / W); p[1] = (uint8_t)(y * 255 / H); p[2] = (uint8_t)((x + y) & 255 ? 128 : 0); p[3] = 255;
    }
    CHECK(arc_image_save_jpeg("v083.jpg", px, W, H, 95)); CHECK(arc_image_save_png("v083.png", px, W, H));
    int w = 0, h = 0; uint8_t *j = arc_image_load("v083.jpg", &w, &h);
    CHECK(j && w == W && h == H);
    if (j) {
        long sum = 0; int mx = 0;
        for (int i = 0; i < W * H * 4; i++) { int d = abs(j[i] - px[i]); sum += d; if (d > mx) mx = d; }
        CHECK(sum / (W * H * 4) <= 2);                              /* a smooth picture at quality 95: close everywhere */
        CHECK(j[3] == 255 && j[(W * H - 1) * 4 + 3] == 255);
        free(j);
    }
    uint8_t *p = arc_image_load("v083.png", &w, &h);
    CHECK(p && w == W && h == H && !memcmp(p, px, sizeof px));      /* PNG through the new inflate: exact */
    free(p);
    arc_set_texture_max_size(64);                                   /* 200 x 120 -> 100 x 60 (decoded at half size) */
    arc_texture t = arc_texture_load_ex("v083.jpg", ARC_TEX_MESH); arc_texture_size(t, &w, &h); CHECK(w == 50 && h == 30);
    arc_set_texture_max_size(128);
    arc_texture u = arc_texture_load_ex("v083.jpg", ARC_TEX_MESH); arc_texture_size(u, &w, &h); CHECK(w == 100 && h == 60);
    arc_set_texture_max_size(13);                                   /* 1/8 (25 x 15), then halved by the cap: 13 x 8 */
    arc_texture v = arc_texture_load_ex("v083.jpg", ARC_TEX_MESH); arc_texture_size(v, &w, &h); CHECK(w == 13 && h == 8);
    arc_set_texture_max_size(0);
    arc_texture_destroy(t); arc_texture_destroy(u); arc_texture_destroy(v);
    remove("v083.jpg"); remove("v083.png");
}
static void v082_init(void) { v082_wrap(); v082_cap(); v083_loading(); }
