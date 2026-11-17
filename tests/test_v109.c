/* ARC 0.10.9 self-test (Droids' web-loading request): two packs whose files are queued interleaved in one group, the
   second pack downloading first, all load; a queued load in tick() gets no "loaded during tick()" hint; arc_pack_fetch
   in a single-package web build fetches nothing; arc_file_listed. Uses test_v086_webload.c's stand-in downloader and
   image decoder (same translation unit). Included by test_core.c; v109_check() runs right after v086w_check().
   Names start with v109_. */
static void v109_check(void) {
    int keep_items = arc__load_items_per_frame;
    size_t pn = 0; uint8_t *pyr = arc__file_read_alloc("pyramid.arcm", &pn, &(bool){ false });
    CHECK(pyr != NULL);
    if (!pyr) return;
    /* 1. packs a and b, three meshes each, queued a b a b a b; b downloads in 1 poll, a in 6 */
    const char *an[3] = { "v109a/m0.arcm", "v109a/m1.arcm", "v109a/m2.arcm" }, *bn[3] = { "v109b/m0.arcm", "v109b/m1.arcm", "v109b/m2.arcm" };
    uint8_t *ds[3] = { pyr, pyr, pyr }; size_t ss[3] = { pn, pn, pn };
    size_t ka = 0, kb = 0; uint8_t *pa = v086w_pack(an, ds, ss, 3, &ka), *pb = v086w_pack(bn, ds, ss, 3, &kb);
    char man[1024];
    snprintf(man, sizeof man, "pack v109a.arcpak %zu 0123456789abcdef\npack v109b.arcpak %zu fedcba9876543210\n"
             "file v109a.arcpak %zu v109a/m0.arcm\nfile v109a.arcpak %zu v109a/m1.arcm\nfile v109a.arcpak %zu v109a/m2.arcm\n"
             "file v109b.arcpak %zu v109b/m0.arcm\nfile v109b.arcpak %zu v109b/m1.arcm\nfile v109b.arcpak %zu v109b/m2.arcm\n", ka, kb, pn, pn, pn, pn, pn, pn);
    arc__fetcher_hook = &v086w_fetcher; arc__imgdec_hook = &v086w_imgdec;
    arc__webload_reset(); arc__pack_manifest_text(man);
    v086w_dl_set("v109a.arcpak", pa, ka, 6, false); v086w_dl_set("v109b.arcpak", pb, kb, 1, false);
    CHECK(arc_file_listed("v109b/m1.arcm") && !arc_file_exists("v109b/m1.arcm") && !arc_file_listed("v109b/nope.arcm"));   /* 4: coming, not here yet */
    arc__load_items_per_frame = 2;   /* (finishing lags behind, so look-ahead runs past unmounted packs) */
    arc_load_begin(ARC_LOAD_BACKGROUND);
    arc_mesh m[6];
    for (int i = 0; i < 3; i++) { m[i * 2] = arc_mesh_load(an[i]); m[i * 2 + 1] = arc_mesh_load(bn[i]); }
    v086w_g = arc_load_end();
    int f = v086w_frames(60, v086w_gdone);
    arc_load_info st = arc_load_status(v086w_g); int ready = 0;
    for (int i = 0; i < 6; i++) ready += arc_mesh_ready(m[i]) && arc_mesh_triangles(m[i]) == 6;
    printf("ARC note interleaved packs: %d frames, %d of 6 meshes, %d failed\n", f, ready, st.failed);
    CHECK(f < 60 && ready == 6 && st.failed == 0 && arc_pack_ready("v109a.arcpak") && arc_pack_ready("v109b.arcpak"));
    CHECK(arc_file_listed("v109b/m1.arcm") && arc_file_exists("v109b/m1.arcm"));
    /* 3. no manifest (a single-package build) and no such file: nothing fetched, a done group, nothing failed */
    arc__webload_reset(); arc__pack_manifest_text(NULL);
    v086w_dl_set("v109c.arcpak", (uint8_t *)calloc(1, 4), 4, 1, true);
    arc_load_group gc = arc_pack_fetch("v109c.arcpak");
    CHECK(arc_load_done(gc) && arc_pack_ready("v109c.arcpak") && v086w_dl_for("v109c.arcpak")->started == 0 && arc_load_status(gc).failed == 0);
    arc__fetcher_hook = NULL; arc__imgdec_hook = NULL; arc__webload_reset();
    arc__load_items_per_frame = keep_items;
    /* 2. a load queued from tick() asks for no "loaded during" hint; a plain one does */
    int ph = arc__phase; arc__phase = ARC__PHASE_TICK;
    int h0 = arc__hint_count("load-in-play");
    arc_load_begin(ARC_LOAD_BACKGROUND); arc_mesh q = arc_mesh_load("pyramid.arcm"); arc_load_group gq = arc_load_end();
    int h1 = arc__hint_count("load-in-play");
    arc_mesh p = arc_mesh_load("pyramid.arcm");
    int h2 = arc__hint_count("load-in-play");
    arc__phase = ph;
    CHECK(q.id && h1 == h0 && p.id && h2 == h0 + 1);
    arc_load_wait(gq); arc_mesh_destroy(q); arc_mesh_destroy(p);
    free(pyr);
}
