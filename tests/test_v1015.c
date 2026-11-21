/* ARC 0.10.15 self-test: glow masks (GTA's car lamps). A mesh's own mask (set, read back, cleared, refused when not a
   texture), and one read from an .arcm's trailing GLOW block (a hand-made QOI image appended to pyramid.arcm), which
   older ARC skips. The pixels are checked on every renderer by the post test (look L_GLOW) and samples/lights
   (tests/glow_mask_check.md). Included by test_core.c (after test_v1014.c); v1015_init() runs in init. Names start with v1015_. */
static size_t v1015_qoi(uint8_t *o) {   /* 2 x 1: red, blue (QOI_OP_RGBA per pixel, then the end marker) */
    size_t n = 0; const uint8_t hdr[14] = { 'q', 'o', 'i', 'f', 0, 0, 0, 2, 0, 0, 0, 1, 4, 0 };
    memcpy(o, hdr, 14); n = 14;
    const uint8_t px[2][4] = { { 255, 0, 0, 255 }, { 0, 0, 255, 255 } };
    for (int i = 0; i < 2; i++) { o[n++] = 0xFF; memcpy(o + n, px[i], 4); n += 4; }
    for (int i = 0; i < 7; i++) o[n++] = 0;
    o[n++] = 1;
    return n;
}
static void v1015_own_mask(void) {
    uint8_t px[4 * 4 * 4]; memset(px, 255, sizeof px);
    arc_texture t = arc_texture_create(4, 4, px, false);
    arc_mesh m = arc_mesh_cube(0xFFFFFF);
    CHECK(arc_mesh_glow_mask(m).id == 0);
    arc_mesh_set_glow_mask(m, t); CHECK(arc_mesh_glow_mask(m).id == t.id);
    arc_mesh_set_glow_mask(m, (arc_texture){ 9999 }); CHECK(arc_mesh_glow_mask(m).id == 0);   /* not a texture: none */
    arc_mesh_set_glow_mask(m, t); arc_mesh_set_glow_mask(m, (arc_texture){ 0 }); CHECK(arc_mesh_glow_mask(m).id == 0);
    arc_mesh_set_glow_mask((arc_mesh){ 0 }, t); CHECK(arc_mesh_glow_mask((arc_mesh){ 0 }).id == 0);   /* no mesh: nothing happens */
    { int w = 0, h = 0; arc_texture_size(t, &w, &h); CHECK(w == 4 && h == 4); }   /* the mesh never owned it */
    arc_mesh_destroy(m); arc_texture_destroy(t);
}
static void v1015_file_mask(void) {
    size_t n = 0, mark = arc_arena_mark(arc_frame()); uint8_t *d = arc_file_read("pyramid.arcm", arc_frame(), &n);
    if (!d) { printf("ARC note glow mask: pyramid.arcm not found\n"); CHECK(false); return; }
    uint8_t *b = (uint8_t *)malloc(n + 64); memcpy(b, d, n); arc_arena_pop(arc_frame(), mark);
    uint8_t q[64]; size_t qn = v1015_qoi(q); uint32_t qs = (uint32_t)qn;
    arc_mesh plain = arc_mesh_from_memory(b, n); CHECK(plain.id && arc_mesh_glow_mask(plain).id == 0);
    memcpy(b + n, "GLOW", 4); memcpy(b + n + 4, &qs, 4); memcpy(b + n + 8, q, qn);
    arc_mesh m = arc_mesh_from_memory(b, n + 8 + qn);
    arc_texture g = arc_mesh_glow_mask(m); int w = 0, h = 0; arc_texture_size(g, &w, &h);
    CHECK(m.id && g.id && w == 2 && h == 1);
    CHECK(arc_mesh_triangles(m) == arc_mesh_triangles(plain));   /* the block changes nothing else */
    arc_mesh_destroy(m);   /* (the mesh owned its mask: freed with it, at the end of the frame)*/
    arc_mesh_destroy(plain); free(b);
}
static void v1015_init(void) { v1015_own_mask(); v1015_file_mask(); }
