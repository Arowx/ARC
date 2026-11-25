/* ARC 0.10.19 self-test: navmesh paths for wide agents on a fine grid (TOY BOX BLITZ's bugs got stuck behind cover).
   Before, every triangle edge shorter than 2 x radius blocked the path, so on half-unit cells from add_grid an agent
   of radius 0.45 found no path at all. Now only a gap between two outline points blocks: open floor of small cells
   is open, and a real gap narrower than the agent still stops it. Included by test_core.c; names start with v1019_. */
static void v1019_init(void) {
    static uint8_t c[40 * 40]; arc_grid g = { 40, 40, c };   /* 20 x 20 units of half-unit cells */
    for (int i = 0; i < 40 * 40; i++) c[i] = 0;
    for (int y = 16; y < 24; y++) for (int x = 16; x < 24; x++) c[y * 40 + x] = 1;   /* a 4 x 4 block in the middle */
    arc_navmesh n = arc_navmesh_create(NULL);
    arc_navmesh_add_grid(n, &g, arc_v3_make(-10, 0, -10), 0.5f, NULL); CHECK(arc_navmesh_build(n));
    arc_v3 p[16]; arc_v3 from = arc_v3_make(0, 0, -6), to = arc_v3_make(0, 0, 6);
    int k = arc_navmesh_path(n, from, to, 0.45f, p, 16);
    CHECK(k >= 3 && k <= 16);                                     /* round the block, not through it */
    bool clear = true;
    for (int i = 0; i < k && i < 16; i++) if (fabsf(p[i].x) < 2.0f + 0.4f && fabsf(p[i].z) < 2.0f + 0.4f) clear = false;   /* corners keep the radius */
    CHECK(clear);
    CHECK(arc_navmesh_path(n, from, to, 0.1f, p, 16) >= 3);
    arc_navmesh_destroy(n);
    /* a wall across the floor with one half-unit gap: too narrow for radius 0.4, wide enough for 0.2 */
    for (int i = 0; i < 40 * 40; i++) c[i] = 0;
    for (int x = 0; x < 40; x++) if (x != 20) c[20 * 40 + x] = 1;
    n = arc_navmesh_create(NULL);
    arc_navmesh_add_grid(n, &g, arc_v3_make(-10, 0, -10), 0.5f, NULL); CHECK(arc_navmesh_build(n));
    CHECK(arc_navmesh_path(n, from, to, 0.4f, p, 16) == 0);
    CHECK(arc_navmesh_path(n, from, to, 0.2f, p, 16) >= 2);
    arc_navmesh_destroy(n);
}
