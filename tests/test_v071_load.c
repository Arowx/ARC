/* ARC 0.7.1 phase D2: background loading and block-compressed textures (included once from test_core.c; every name
   here starts with v071l_ because the test files share one translation unit). Headless: the software renderer, so
   compressed textures take the CPU path here; tests/test_post.c's L_CTEX look checks the GPU paths. */

extern int arc__load_items_per_frame;              /* src/arc_load.c: files finished per frame (0 = no limit) */
static arc_texture v071l_target;                   /* pending handles drawn into it at init: must stay empty */
static arc_texture v071l_screen;                   /* ARC's loading screen drawn into it */
static int v071l_cb_count, v071l_cb_late;
static void v071l_cb(void *user) { (*(int *)user)++; }

/* ---- decoders: blocks written by hand, texels worked out by hand (src/arc_texc.c's formulas) ---- */
static const uint8_t v071l_bc1_4[8] = { 0x00, 0xF8, 0x1F, 0x00, 0xE4, 0x1B, 0x50, 0xFA };   /* red > blue: 4 colours */
static const uint8_t v071l_bc1_3[8] = { 0xE0, 0x07, 0xFF, 0xFF, 0xE4, 0x1B, 0x50, 0xFA };   /* green < white: 3 + cut-out */
static const uint8_t v071l_a8[8] = { 245, 0, 0x88, 0xC6, 0xFA, 0x00, 0x00, 0x00 };         /* texels 0..7: codes 0..7 */
static const uint8_t v071l_a6[8] = { 0, 250, 0x88, 0xC6, 0xFA, 0x00, 0x00, 0x00 };
static const uint8_t v071l_bc7_m6[16] = { 0x40, 0xC0, 0x1F, 0xF0, 0x07, 0x00, 0xFE, 0x7F, 0x11, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE };
static const uint8_t v071l_bc7_m1[16] = { 0x8A, 0x4A, 0xE5, 0xF1, 0xA9, 0x41, 0x06, 0xA0, 0x95, 0x6A, 0x26, 0xAF, 0xBC, 0xCD, 0xAF, 0xE5 };
static bool v071l_rgba(const uint8_t *p, int r, int g, int b, int a) { return p[0] == r && p[1] == g && p[2] == b && p[3] == a; }
static void v071l_decoders(void) {
    uint8_t px[64];
    /* BC1, 4 colours: index rows 0 1 2 3 / 3 2 1 0 / 0 0 1 1 / 2 2 3 3 */
    static const int idx[16] = { 0, 1, 2, 3, 3, 2, 1, 0, 0, 0, 1, 1, 2, 2, 3, 3 };
    static const int pal4[4][4] = { { 255, 0, 0, 255 }, { 0, 0, 255, 255 }, { 170, 0, 85, 255 }, { 85, 0, 170, 255 } };
    static const int pal3[4][4] = { { 0, 255, 0, 255 }, { 255, 255, 255, 255 }, { 128, 255, 128, 255 }, { 0, 0, 0, 0 } };
    arc__bc1_block(v071l_bc1_4, px, false);
    bool ok = true; for (int i = 0; i < 16; i++) ok = ok && v071l_rgba(px + i * 4, pal4[idx[i]][0], pal4[idx[i]][1], pal4[idx[i]][2], pal4[idx[i]][3]);
    CHECK(ok);
    arc__bc1_block(v071l_bc1_3, px, false);
    ok = true; for (int i = 0; i < 16; i++) ok = ok && v071l_rgba(px + i * 4, pal3[idx[i]][0], pal3[idx[i]][1], pal3[idx[i]][2], pal3[idx[i]][3]);
    CHECK(ok);
    arc__bc1_block(v071l_bc1_3, px, true);                 /* BC3's colour: always 4 colours, never cut out */
    CHECK(v071l_rgba(px + 2 * 4, 85, 255, 85, 255) && v071l_rgba(px + 3 * 4, 170, 255, 170, 255));   /* (2g+w+1)/3, (g+2w+1)/3 */
    static const uint8_t grey[8] = { 0x10, 0x84, 0x41, 0x08, 0, 0, 0, 0 };   /* 565 -> 888 by bit replication */
    arc__bc1_block(grey, px, false); CHECK(v071l_rgba(px, 132, 130, 132, 255));
    uint8_t g2[8]; memcpy(g2, grey, 8); g2[4] = 1; arc__bc1_block(g2, px, false); CHECK(v071l_rgba(px, 8, 8, 8, 255));   /* 0x0841 */
    static const uint8_t rnd[8] = { 0xFF, 0xFF, 0x40, 0x00, 0xE4, 0, 0, 0 };   /* white, g 8: thirds round to nearest */
    arc__bc1_block(rnd, px, false); CHECK(v071l_rgba(px + 8, 170, 173, 170, 255) && v071l_rgba(px + 12, 85, 90, 85, 255));   /* (518+1)/3, (271+1)/3 */
    /* BC4: 8 levels (245..0 in sevenths) and 6 levels (+ 0 and 255) */
    static const int v8[8] = { 245, 0, 210, 175, 140, 105, 70, 35 }, v6[8] = { 0, 250, 50, 100, 150, 200, 0, 255 };
    uint8_t a[16]; ok = true;
    arc__bc4_block(v071l_a8, a, 1); for (int i = 0; i < 16; i++) ok = ok && a[i] == (i < 8 ? v8[i] : 245);
    arc__bc4_block(v071l_a6, a, 1); for (int i = 0; i < 16; i++) ok = ok && a[i] == (i < 8 ? v6[i] : 0);
    CHECK(ok);
    static const uint8_t r7[8] = { 255, 1, 0x88, 0xC6, 0xFA, 0, 0, 0 };   /* rounding: (6*255 + 1 + 3) / 7 = 219 */
    arc__bc4_block(r7, a, 1); CHECK(a[2] == 219 && a[7] == (255 + 6 + 3) / 7);
    /* BC5: X, Y and Z rebuilt (204, 128 -> Z 0.8 -> 229) */
    uint8_t b5[16] = { 204, 0, 0, 0, 0, 0, 0, 0, 128, 0, 0, 0, 0, 0, 0, 0 };
    arc__bc5_block(b5, px); CHECK(v071l_rgba(px, 204, 128, 229, 255) && v071l_rgba(px + 60, 204, 128, 229, 255));
    b5[0] = 255; b5[8] = 128; arc__bc5_block(b5, px); CHECK(px[2] == 128);   /* (1, 0): Z = 0 */
    /* BC7 mode 6 (by hand) and mode 1 (two subsets, partition 2: values from Pillow's decoder) */
    static const int w4[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };
    arc__bc7_block(v071l_bc7_m6, px); ok = true;
    for (int i = 0; i < 16; i++) {   /* e0 (0, 0, 0, 254), e1 (255, 255, 1, 255), texel i has index i */
        int r = (w4[i] * 255 + 32) >> 6, b = (w4[i] + 32) >> 6, al = ((64 - w4[i]) * 254 + w4[i] * 255 + 32) >> 6;
        ok = ok && v071l_rgba(px + i * 4, r, r, b, al);
    }
    CHECK(ok && v071l_rgba(px + 8 * 4, 135, 135, 1, 255));
    arc__bc7_block(v071l_bc7_m1, px);
    CHECK(v071l_rgba(px, 46, 145, 123, 255) && v071l_rgba(px + 4 * 4, 209, 46, 123, 255) && v071l_rgba(px + 11 * 4, 243, 6, 106, 255) && v071l_rgba(px + 13 * 4, 59, 106, 112, 255));
    uint8_t m8[16]; memset(m8, 0, 16); arc__bc7_block(m8, px); CHECK(v071l_rgba(px, 0, 0, 0, 0));   /* reserved mode: transparent */
    /* a whole level: 6 x 6 is 2 x 2 blocks, cropped; too little data is refused */
    uint8_t lv[32], out[6 * 6 * 4]; for (int b = 0; b < 4; b++) memcpy(lv + b * 8, b == 3 ? v071l_bc1_3 : v071l_bc1_4, 8);
    CHECK(arc__bc_decode(ARC_TEXFMT_BC1, lv, 32, 6, 6, out) && v071l_rgba(out + (5 * 6 + 4) * 4, 0, 0, 0, 0) && v071l_rgba(out + (4 * 6 + 4) * 4, 0, 255, 0, 255) && v071l_rgba(out + (4 * 6 + 5) * 4, 255, 255, 255, 255));
    CHECK(!arc__bc_decode(ARC_TEXFMT_BC1, lv, 31, 6, 6, out) && !arc__bc_decode(2, lv, 32, 6, 6, out));
}

/* ---- .arct files ---- */
static size_t v071l_arct(uint8_t *o, int fmt, int w, int h, int levels, int flags, const uint8_t *payload, size_t n) {
    uint32_t hd[7] = { 1, (uint32_t)fmt, (uint32_t)w, (uint32_t)h, (uint32_t)levels, (uint32_t)flags, (uint32_t)n };
    memcpy(o, "ARCT", 4); memcpy(o + 4, hd, sizeof hd); memcpy(o + 32, payload, n); return 32 + n;
}
static size_t v071l_zlib_stored(uint8_t *o, const uint8_t *d, size_t n) {   /* a zlib stream of one stored block */
    uint32_t s1 = 1, s2 = 0; for (size_t i = 0; i < n; i++) { s1 = (s1 + d[i]) % 65521; s2 = (s2 + s1) % 65521; }
    o[0] = 0x78; o[1] = 0x01; o[2] = 1; o[3] = (uint8_t)n; o[4] = (uint8_t)(n >> 8); o[5] = (uint8_t)~n; o[6] = (uint8_t)(~n >> 8);
    memcpy(o + 7, d, n); uint32_t ad = s2 << 16 | s1;
    o[7 + n] = (uint8_t)(ad >> 24); o[8 + n] = (uint8_t)(ad >> 16); o[9 + n] = (uint8_t)(ad >> 8); o[10 + n] = (uint8_t)ad;
    return 11 + n;
}
static uint8_t v071l_file_b[32 + 40], v071l_file_bz[32 + 64]; static size_t v071l_nb, v071l_nbz;
static void v071l_container(void) {
    /* 8 x 4 BC1 (red..blue, green/white) + a 4 x 2 level (one block, grey) */
    uint8_t pay[24]; memcpy(pay, v071l_bc1_4, 8); memcpy(pay + 8, v071l_bc1_3, 8);
    static const uint8_t grey[8] = { 0x10, 0x84, 0x00, 0x00, 0, 0, 0, 0 }; memcpy(pay + 16, grey, 8);
    v071l_nb = v071l_arct(v071l_file_b, ARC_TEXFMT_BC1, 8, 4, 2, 0, pay, 24);
    uint8_t z[64]; size_t zn = v071l_zlib_stored(z, pay, 24);
    v071l_nbz = v071l_arct(v071l_file_bz, ARC_TEXFMT_BC1, 8, 4, 2, 2, z, zn);
    int w = 0, h = 0; uint8_t *d = arc_image_decode(v071l_file_b, v071l_nb, &w, &h), *dz = arc_image_decode(v071l_file_bz, v071l_nbz, &w, &h);
    CHECK(d && dz && w == 8 && h == 4 && !memcmp(d, dz, 8 * 4 * 4));
    CHECK(d && v071l_rgba(d + 2 * 4, 170, 0, 85, 255) && v071l_rgba(d + 7 * 4, 0, 0, 0, 0) && v071l_rgba(d + (3 * 8 + 4) * 4, 128, 255, 128, 255));
    free(d); free(dz);
    arc_texture t = arc_texture_from_memory(v071l_file_bz, v071l_nbz, ARC_TEX_MESH);   /* soft renderer: decoded, with the file's mips */
    CHECK(t.id && arc_texture_format(t) == ARC_TEXFMT_RGBA8 && !arc_texture_format_supported(ARC_TEXFMT_BC1));
    const arc__tex_rec *r = &arc__textures[t.id];
    CHECK(r->levels == 2 && r->mw[1] == 4 && r->mh[1] == 2 && v071l_rgba(r->mip[1], 132, 130, 132, 255));   /* level 1 = the file's grey */
    arc_texture t0 = arc_texture_from_memory(v071l_file_b, v071l_nb, 0);                  /* no ARC_TEX_MIPMAPS: one level */
    CHECK(t0.id && arc__textures[t0.id].levels == 1 && !memcmp(arc__textures[t0.id].rgba, r->rgba, 8 * 4 * 4));
    int tw, th; arc_texture_size(t0, &tw, &th); CHECK(tw == 8 && th == 4);
    uint8_t bad[sizeof v071l_file_b]; memcpy(bad, v071l_file_b, v071l_nb);
    bad[4] = 2; CHECK(!arc_texture_from_memory(bad, v071l_nb, 0).id);                     /* unknown version */
    bad[4] = 1; bad[8] = 6; CHECK(!arc_texture_from_memory(bad, v071l_nb, 0).id);          /* unknown format */
    bad[8] = 1; CHECK(!arc_texture_from_memory(bad, v071l_nb - 1, 0).id);                  /* truncated */
    bad[16] = 0; bad[17] = 1; CHECK(!arc_texture_from_memory(bad, v071l_nb, 0).id);       /* 256 high: too little data */
    arc_gfx_stats st = arc_stats(); CHECK(st.textures > 2 && st.textures_compressed == 0 && st.texture_bytes > 8 * 4 * 4);
    uint8_t up[8 * 4 * 4]; memset(up, 77, sizeof up); arc_texture_update(t0, up); CHECK(arc__textures[t0.id].rgba[5] == 77);
}

/* ---- meshes: a version 4 file (compressed texture and BC5 normal map), and the old versions ---- */
static size_t v071l_mesh4(uint8_t *o, const uint8_t *tex, size_t tn, const uint8_t *nmap, size_t nn) {
    size_t at = 0; uint32_t hd[7] = { 4, 3, 3, 4, (uint32_t)tn, (uint32_t)(52 + nn), 0 };
    memcpy(o, "ARCM", 4); memcpy(o + 4, hd, sizeof hd); at = 32;
    float lo[3] = { 0, 0, 0 }, hi[3] = { 1, 1, 0 }; memcpy(o + at, lo, 12); memcpy(o + at + 12, hi, 12); at += 24;
    static const uint16_t P[3][3] = { { 0, 0, 0 }, { 65535, 0, 0 }, { 0, 65535, 0 } };
    for (int i = 0; i < 3; i++) {   /* u16 position, octahedral normal (0, 0, 1), white, half u v */
        memcpy(o + at, P[i], 6); o[at + 6] = 0; o[at + 7] = 0; memset(o + at + 8, 255, 4);
        uint16_t u = i == 1 ? 0x3C00 : 0, v = i == 2 ? 0x3C00 : 0; memcpy(o + at + 12, &u, 2); memcpy(o + at + 14, &v, 2); at += 16;
    }
    uint16_t idx[4] = { 0, 1, 2, 0 }; memcpy(o + at, idx, 8); at += 8;
    memcpy(o + at, tex, tn); at += tn;
    uint32_t mf = 0, sz[3] = { (uint32_t)nn, 0, 0 }; float f[7] = { 1, 0, 1, 0, 0, 0, 0 };
    memcpy(o + at, "MATL", 4); uint32_t one = 1; memcpy(o + at + 4, &one, 4); memcpy(o + at + 8, &mf, 4); memcpy(o + at + 12, f, 28); memcpy(o + at + 40, sz, 12);
    memcpy(o + at + 52, nmap, nn); at += 52 + nn;
    return at;
}
static void v071l_meshes(void) {
    uint8_t nb[16] = { 204, 0, 0, 0, 0, 0, 0, 0, 128, 0, 0, 0, 0, 0, 0, 0 }, nf[64], mf[512];
    size_t nn = v071l_arct(nf, ARC_TEXFMT_BC5, 4, 4, 1, 1, nb, 16);
    size_t n = v071l_mesh4(mf, v071l_file_b, v071l_nb, nf, nn);
    arc_mesh m = arc_mesh_from_memory(mf, n);
    CHECK(m.id && arc_mesh_triangles(m) == 1);
    arc_texture t = arc_mesh_texture(m); int w = 0, h = 0; arc_texture_size(t, &w, &h);
    CHECK(t.id && w == 8 && h == 4 && v071l_rgba(arc__textures[t.id].rgba + 4, 0, 0, 255, 255));
    arc_material mat; CHECK(arc_mesh_material(m, &mat) && mat.normal.id && mat.roughness == 1.0f);
    CHECK(mat.normal.id && v071l_rgba(arc__textures[mat.normal.id].rgba, 204, 128, 229, 255));   /* Z rebuilt on the CPU */
    uint8_t old[512]; memcpy(old, mf, n); old[4] = 5; CHECK(!arc_mesh_from_memory(old, n).id);   /* a version from the future */
    /* version 1 (no uv), written here; v2 (36-byte vertices) and v3 by tools/mesh2arcm.py; AXE's .axm */
    uint8_t v1[16 + 3 * 28]; memset(v1, 0, sizeof v1); memcpy(v1, "ARCM", 4); v1[4] = 1; v1[8] = 3;
    float p1[3][3] = { { 0, 0, 0 }, { 2, 0, 0 }, { 0, 3, 0 } }; for (int i = 0; i < 3; i++) { memcpy(v1 + 16 + i * 28, p1[i], 12); memset(v1 + 16 + i * 28 + 24, 255, 4); }
    arc_mesh m1 = arc_mesh_from_memory(v1, sizeof v1); arc_v3 lo, hi; arc_mesh_bounds(m1, &lo, &hi);
    CHECK(m1.id && arc_mesh_triangles(m1) == 1 && feq(hi.x, 2) && feq(hi.y, 3));
    arc_mesh m2 = arc_mesh_load("pyramid_v2.arcm"), m3 = arc_mesh_load("pyramid.arcm"), ma = arc_mesh_load("pyramid.axm");
    CHECK(m2.id && m3.id && ma.id && arc_mesh_triangles(m2) == 6 && arc_mesh_triangles(m3) == 6 && arc_mesh_triangles(ma) == 6);
    arc_mesh_destroy(m); arc_mesh_destroy(m1); arc_mesh_destroy(m2); arc_mesh_destroy(m3); arc_mesh_destroy(ma);
}

/* ---- the load queue ---- */
static arc_texture v071l_t1, v071l_t2, v071l_t3, v071l_tnew; static arc_mesh v071l_m1, v071l_m2, v071l_m3; static arc_sound v071l_s1; static arc_music v071l_mu;
static arc_load_group v071l_g1; static arc_texture v071l_pd_t; static arc_mesh v071l_pd_m;
static uint8_t v071l_png[16 * 8 * 4];
static void v071l_queue(void) {
    for (int i = 0; i < 16 * 8; i++) { v071l_png[i * 4] = (uint8_t)(i * 2); v071l_png[i * 4 + 1] = (uint8_t)(255 - i); v071l_png[i * 4 + 2] = 9; v071l_png[i * 4 + 3] = 255; }
    CHECK(arc_image_save_png("v071l_a.png", v071l_png, 16, 8));
    CHECK(arc_file_write("v071l_b.arct", v071l_file_bz, v071l_nbz) && arc_file_write("v071l_m.qoa", test_qoa, sizeof test_qoa));   /* (test_core.c's) */
    CHECK(arc_file_write("v071l_bad.png", "\x89PNG nonsense", 13) && arc_file_write("v071l_bad.arcm", "ARCM\x03\0\0\0", 8));
    /* step mode: one file a frame, so the in-between states can be checked */
    CHECK(arc_load_progress((arc_load_group){ 0 }) == 1.0f && arc_load_done((arc_load_group){ 0 }));   /* nothing queued yet */
    arc__load_items_per_frame = 1;
    arc_load_begin(0);
    v071l_t1 = arc_texture_load("v071l_a.png");
    v071l_t2 = arc_texture_load_ex("v071l_b.arct", ARC_TEX_MESH);
    v071l_m1 = arc_mesh_load("pyramid.arcm");
    v071l_m2 = arc_mesh_load("inpack/pyr.arcm");                 /* from test.arcpak (mounted by v05_pack) */
    v071l_s1 = arc_sound_load("v071l_m.qoa");
    v071l_mu = arc_music_load("v071l_m.qoa");
    CHECK(!arc_texture_load("v071l_missing.png").id && !arc_mesh_load("v071l_missing.arcm").id);   /* missing: {0} at once */
    CHECK(v071l_t1.id && v071l_t2.id && v071l_m1.id && v071l_m2.id && v071l_s1.id && v071l_mu.id);
    /* handles before they are ready: safe and empty */
    int w = 1, h = 1; arc_texture_size(v071l_t1, &w, &h); arc_v3 lo, hi; arc_mesh_bounds(v071l_m1, &lo, &hi);
    CHECK(w == 0 && h == 0 && !arc_texture_ready(v071l_t1) && !arc_mesh_ready(v071l_m1) && arc_mesh_triangles(v071l_m1) == 0);
    CHECK(feq(hi.x, 0) && !arc_sound_ready(v071l_s1) && !arc_music_ready(v071l_mu) && !arc_play(v071l_s1, 1, 0).id && arc_sound_length(v071l_s1) == 0);
    arc_music_play(v071l_mu, true); CHECK(!arc_music_playing(v071l_mu));                 /* starts when ready */
    arc_mesh_set_texture(v071l_m1, v071l_t1); arc_mesh_set_cast_shadow(v071l_m1, false);  /* kept when it loads */
    arc_texture_update(v071l_t1, v071l_png);                                               /* ignored while loading */
    arc_load_info s = arc_load_status((arc_load_group){ 0 });
    CHECK(s.total == 6 && s.done == 0 && s.failed == 0 && s.bytes_total > 0 && s.bytes_done == 0 && s.progress == 0 && !arc_load_done((arc_load_group){ 0 }));
    v071l_g1 = arc_load_end();
    CHECK(v071l_g1.id && !arc_load_done(v071l_g1) && arc_load_status(v071l_g1).total == 6);
    arc_load_on_done(v071l_g1, v071l_cb, &v071l_cb_count);
    long long first = arc__file_size("v071l_a.png", &(bool){ false });
    CHECK(arc__load_frame() && arc_load_status(v071l_g1).done == 1);                       /* ticks wait (flags 0) */
    s = arc_load_status(v071l_g1);
    CHECK(s.bytes_done == first && feq(s.progress, (float)((double)first / (double)s.bytes_total)) && arc_texture_ready(v071l_t1) && !arc_texture_ready(v071l_t2));
    int frames = 1; while (arc__load_frame()) frames++;                                    /* the 6th call finishes the last file */
    CHECK(frames == 5 && arc_load_done(v071l_g1) && arc_load_progress(v071l_g1) == 1.0f && v071l_cb_count == 1);
    arc_load_on_done(v071l_g1, v071l_cb, &v071l_cb_late); CHECK(v071l_cb_late == 1);      /* after: at once */
    s = arc_load_status(v071l_g1); CHECK(s.done == 6 && s.failed == 0 && s.bytes_done == s.bytes_total && s.seconds >= 0);
    /* everything ready, settings made meanwhile kept */
    arc_texture_size(v071l_t1, &w, &h); CHECK(w == 16 && h == 8 && !memcmp(arc__textures[v071l_t1.id].rgba, v071l_png, sizeof v071l_png));
    arc_texture_size(v071l_t2, &w, &h); CHECK(w == 8 && h == 4 && arc__textures[v071l_t2.id].levels == 2 && arc__textures[v071l_t2.id].flags == ARC_TEX_MESH);
    CHECK(arc_mesh_ready(v071l_m1) && arc_mesh_triangles(v071l_m1) == 6 && arc_mesh_texture(v071l_m1).id == v071l_t1.id && arc__meshes[v071l_m1.id].no_shadow);
    arc_mesh_bounds(v071l_m1, &lo, &hi); CHECK(feq(lo.y, 0) && feq(hi.y, 1));
    CHECK(arc_mesh_ready(v071l_m2) && arc_mesh_triangles(v071l_m2) == 6);
    CHECK(arc_sound_ready(v071l_s1) && feq(arc_sound_length(v071l_s1), 300.0f / 44100) && arc_music_ready(v071l_mu) && arc_music_playing(v071l_mu));
    arc_music_stop(v071l_mu);
    arc__load_items_per_frame = 0;
    /* headless default: arc_load_end finishes the group; the same data as loading at once (and as the steps above) */
    arc_load_begin(0);
    v071l_t3 = arc_texture_load("v071l_a.png"); v071l_m3 = arc_mesh_load("pyramid.arcm");
    arc_texture tb = arc_texture_load("v071l_bad.png"); arc_mesh mb = arc_mesh_load("v071l_bad.arcm");
    arc_load_group g2 = arc_load_end();
    s = arc_load_status(g2);
    CHECK(arc_load_done(g2) && s.total == 4 && s.done == 4 && s.failed == 2 && !arc__load_frame());
    CHECK(tb.id && !arc_texture_ready(tb) && mb.id && !arc_mesh_ready(mb) && arc_mesh_triangles(mb) == 0);   /* failed: stay empty */
    arc_texture_size(tb, &w, &h); CHECK(w == 0 && h == 0);
    arc_texture ts = arc_texture_load("v071l_a.png"); arc_mesh ms = arc_mesh_load("pyramid.arcm");
    CHECK(!memcmp(arc__textures[v071l_t3.id].rgba, arc__textures[ts.id].rgba, sizeof v071l_png) && arc__textures[v071l_t3.id].flags == arc__textures[ts.id].flags);
    CHECK(arc__meshes[v071l_m3.id].vcount == arc__meshes[ms.id].vcount && !memcmp(arc__meshes[v071l_m3.id].v, arc__meshes[ms.id].v, sizeof(arc_vertex) * (size_t)arc__meshes[ms.id].vcount));
    /* destroyed before ready: dropped, and a texture made meanwhile in the same slot is left alone */
    arc_load_begin(ARC_LOAD_BACKGROUND);
    arc_texture tq = arc_texture_load("v071l_a.png"); arc_mesh mq = arc_mesh_load("pyramid.arcm");
    arc_texture_destroy(tq); arc_mesh_destroy(mq);
    uint8_t red[4 * 4 * 4]; for (int i = 0; i < 16; i++) { red[i * 4] = 255; red[i * 4 + 1] = red[i * 4 + 2] = 0; red[i * 4 + 3] = 255; }
    v071l_tnew = arc_texture_create(4, 4, red, false);
    arc_mesh mnew = arc_mesh_cube(0x00FF00);
    arc_load_group g3 = arc_load_end();
    CHECK(arc_load_done(g3) && arc_load_status(g3).total == 2 && mnew.id == mq.id);          /* the cube took the queued mesh's slot */
    arc_texture_size(v071l_tnew, &w, &h); CHECK(w == 4 && h == 4 && v071l_rgba(arc__textures[v071l_tnew.id].rgba, 255, 0, 0, 255));
    CHECK(arc_mesh_triangles(mnew) == 12);
    /* misuse is harmless */
    arc_load_begin(0); arc_load_begin(0);                                                 /* no nesting: logged */
    CHECK(arc_load_end().id != 0 && arc_load_end().id == 0);                              /* end without begin: {0} */
    /* hot reload of a queued texture: the same handle takes a new file (.arct after .png, and back) */
    CHECK(arc__texture_reload(v071l_t3, "v071l_b.arct"));
    arc_texture_size(v071l_t3, &w, &h); CHECK(w == 8 && h == 4 && v071l_rgba(arc__textures[v071l_t3.id].rgba + 8, 170, 0, 85, 255));
    CHECK(arc__texture_reload(v071l_t3, "v071l_a.png")); arc_texture_size(v071l_t3, &w, &h); CHECK(w == 16 && h == 8);
    CHECK(arc__mesh_reload(v071l_m3, "pyramid_v2.arcm") && arc_mesh_triangles(v071l_m3) == 6);
    /* drawing handles that are still loading: nothing (this background group finishes a file a frame from here on,
       after init has drawn them into a target) */
    arc__load_items_per_frame = 1;
    arc_load_begin(ARC_LOAD_BACKGROUND);
    v071l_pd_t = arc_texture_load("v071l_a.png"); v071l_pd_m = arc_mesh_load("pyramid.arcm");
    arc_load_end();
    v071l_target = arc_target_create(16, 16, false);
    arc_target_begin(v071l_target, true, 0xFF102030);
    arc_camera cam = { { 0, 0.5f, 3 }, { 0, 0.5f, 0 }, { 0, 1, 0 }, 1.0f, 0, 0.1f, 50 }; arc_set_camera(&cam);
    arc_draw_mesh(v071l_pd_m, arc_m4_identity(), 0xFFFFFF);
    arc_draw_texture_ex(v071l_pd_t, 0, 0, 16, 8, 0, 0, 16, 16, 0, 0, 0, ARC_WHITE);
    arc_target_end();
    /* ARC's loading screen: a bar filled to the progress */
    v071l_screen = arc_target_create(160, 90, false);
    arc_target_begin(v071l_screen, true, 0xFF000000); arc_draw_loading_screen(0.5f, "Loading"); arc_target_end();
}
static void v071l_check(void) {
    const uint8_t *p = arc__textures[v071l_target.id].rgba; bool clear = true;   /* the pending meshes and texture drew nothing */
    for (int i = 0; i < 16 * 16; i++) clear = clear && v071l_rgba(p + i * 4, 0x10, 0x20, 0x30, 255);
    CHECK(clear && arc_texture_ready(v071l_pd_t) && arc_mesh_ready(v071l_pd_m));   /* ...and loaded since (a file a frame) */
    const uint8_t *s = arc__textures[v071l_screen.id].rgba;
    int y = 45 + 2; /* the bar: (w - bw) / 2 .. + bw, at h / 2; filled half way */
    CHECK(s[(y * 160 + 50) * 4] > 150 && s[(y * 160 + 110) * 4] < 100 && s[(y * 160 + 110) * 4] > 20);
    CHECK(s[(5 * 160 + 5) * 4] < 10);     /* a corner: background */
}
static void v071l_init(void) { v071l_decoders(); v071l_container(); v071l_meshes(); v071l_queue(); }
