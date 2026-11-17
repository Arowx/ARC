/* ARC 0.9.2 self-test: navmeshes and agents. Included by test_core.c; v092_init() runs in init. Names start with
   v092_ (one translation unit). */
static arc_grid v092_g;
static void v092_grid(int w, int h, const char **rows) {   /* '#' blocked, anything else walkable (id 0) */
    free(v092_g.cells); v092_g.cells = NULL;
    arc_grid_init(&v092_g, NULL, w, h);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) arc_grid_set(&v092_g, x, y, rows[y][x] == '#');
}
static bool v092_all_walkable(arc_navmesh nm, const arc_v3 *p, int n) {
    for (int i = 1; i < n; i++) if (!arc_navmesh_walkable_line(nm, p[i - 1], p[i], NULL)) return false;
    return true;
}
static void v092_paths(void) {
    static const char *rows[] = {
        ".....#....",
        ".....#....",
        ".....#....",
        ".....#....",
        ".....#....",
        ".....#....",
        ".....#....",
        ".....#....",
        "..........",
        "..........",
    };
    v092_grid(10, 10, rows);
    arc_navmesh nm = arc_navmesh_create(NULL);
    CHECK(nm.id != 0);
    arc_navmesh_add_grid(nm, &v092_g, arc_v3_make(0, 0, 0), 1, NULL);
    CHECK(arc_navmesh_build(nm));
    CHECK(arc_navmesh_triangles(nm) == 2 * (100 - 8));
    CHECK(arc_navmesh_islands(nm) == 1);
    arc_v3 p[64];
    /* around the wall through the gap at the bottom (grid y 8..9 = z 8..10) */
    int n = arc_navmesh_path(nm, arc_v3_make(1.5f, 0, 1.5f), arc_v3_make(8.5f, 0, 1.5f), 0, p, 64);
    CHECK(n >= 3 && n <= 4);
    float len = arc_navmesh_path_length(p, n);
    CHECK(len > 14.0f && len < 15.5f);                     /* the shortest way touches the wall's end corners */
    bool onm = true; for (int i = 0; i < n; i++) { bool f; arc_navmesh_height(nm, p[i], &f); onm = onm && f; }
    CHECK(onm);                                            /* radius 0 runs along the wall's edge: points on the mesh */
    CHECK(fabsf(p[0].x - 1.5f) < 1e-4f && fabsf(p[n - 1].x - 8.5f) < 1e-4f);
    bool via_gap = false; for (int i = 1; i < n - 1; i++) if (p[i].z >= 7.99f) via_gap = true;
    CHECK(via_gap);
    /* radius: the corners keep away from the wall's end (its corners are (5, 8) and (6, 8)) */
    int nr = arc_navmesh_path(nm, arc_v3_make(1.5f, 0, 1.5f), arc_v3_make(8.5f, 0, 1.5f), 0.4f, p, 64);
    CHECK(nr >= 3);
    float mind = 1e9f;
    for (int i = 1; i < nr - 1; i++) { float a = arc_v3_len(arc_v3_sub(p[i], arc_v3_make(5, 0, 8))), b = arc_v3_len(arc_v3_sub(p[i], arc_v3_make(6, 0, 8))); mind = fminf(mind, fminf(a, b)); }
    CHECK(mind > 0.39f && mind < 0.41f);
    CHECK(v092_all_walkable(nm, p, nr));
    CHECK(arc_navmesh_path_length(p, nr) > len);
    /* open ground: a straight line, two points */
    CHECK(arc_navmesh_path(nm, arc_v3_make(0.5f, 0, 0.5f), arc_v3_make(4.5f, 0, 9.5f), 0, p, 64) == 2);
    /* a walk through the wall stops at it */
    arc_v3 hit;
    CHECK(!arc_navmesh_walkable_line(nm, arc_v3_make(1.5f, 0, 3.5f), arc_v3_make(8.5f, 0, 3.5f), &hit));
    CHECK(fabsf(hit.x - 5.0f) < 1e-3f && fabsf(hit.z - 3.5f) < 1e-3f);
    CHECK(arc_navmesh_walkable_line(nm, arc_v3_make(1.5f, 0, 9.0f), arc_v3_make(8.5f, 0, 9.0f), NULL));
    /* nearest walkable point to a point inside the wall: the wall's side */
    arc_v3 q;
    CHECK(arc_navmesh_nearest(nm, arc_v3_make(5.3f, 0, 3.5f), 1, &q));
    CHECK(fabsf(q.x - 5.0f) < 1e-3f && fabsf(q.z - 3.5f) < 1e-3f);
    CHECK(!arc_navmesh_nearest(nm, arc_v3_make(5.5f, 3, 3.5f), 0.5f, &q));   /* nothing within 0.5 */
    /* an unreachable target: the path ends at the nearest walkable point */
    n = arc_navmesh_path(nm, arc_v3_make(1.5f, 0, 1.5f), arc_v3_make(5.5f, 0, 2.5f), 0, p, 64);
    CHECK(n == 2 && fabsf(p[1].x - 5.0f) < 1e-3f);
    /* cut at max: the full count is returned */
    CHECK(arc_navmesh_path(nm, arc_v3_make(1.5f, 0, 1.5f), arc_v3_make(8.5f, 0, 1.5f), 0, p, 2) >= 3);
    /* random points: on the surface, even, the same with the same seed */
    arc_rng r1, r2; arc_rng_seed(&r1, 7, 1); arc_rng_seed(&r2, 7, 1);
    int left = 0, on = 0, same = 0;
    for (int i = 0; i < 400; i++) {
        arc_v3 a = arc_navmesh_random_point(nm, &r1), b = arc_navmesh_random_point(nm, &r2);
        bool f; arc_navmesh_height(nm, a, &f); on += f; left += a.x < 5; same += a.x == b.x && a.z == b.z;
    }
    CHECK(on == 400 && same == 400);
    CHECK(left > 180 && left < 250);                       /* 50 of the 92 cells are left of the wall */
    arc_navmesh_destroy(nm);
}
static void v092_islands_and_width(void) {
    static const char *closed[] = { "..#..", "..#..", "..#.." };
    v092_grid(5, 3, closed);
    arc_navmesh nm = arc_navmesh_create(NULL);
    arc_navmesh_add_grid(nm, &v092_g, arc_v3_make(0, 0, 0), 1, NULL); arc_navmesh_build(nm);
    CHECK(arc_navmesh_islands(nm) == 2);
    arc_v3 p[8];
    CHECK(arc_navmesh_path(nm, arc_v3_make(0.5f, 0, 1.5f), arc_v3_make(4.5f, 0, 1.5f), 0, p, 8) == 0);
    CHECK(!arc_navmesh_connected(nm, arc_v3_make(0.5f, 0, 1.5f), arc_v3_make(4.5f, 0, 1.5f)));
    CHECK(arc_navmesh_connected(nm, arc_v3_make(0.5f, 0, 1.5f), arc_v3_make(1.5f, 0, 0.5f)));
    arc_navmesh_destroy(nm);
    /* a doorway one cell wide: an agent of radius 0.4 fits, 0.6 does not */
    static const char *door[] = { "..#..", ".....", "..#.." };
    v092_grid(5, 3, door);
    nm = arc_navmesh_create(NULL);
    arc_navmesh_add_grid(nm, &v092_g, arc_v3_make(0, 0, 0), 1, NULL); arc_navmesh_build(nm);
    CHECK(arc_navmesh_path(nm, arc_v3_make(0.5f, 0, 0.5f), arc_v3_make(4.5f, 0, 0.5f), 0.4f, p, 8) >= 3);
    CHECK(arc_navmesh_path(nm, arc_v3_make(0.5f, 0, 0.5f), arc_v3_make(4.5f, 0, 0.5f), 0.6f, p, 8) == 0);
    arc_navmesh_destroy(nm);
}
static void v092_soup_and_meshes(void) {
    /* a floor 0..4 x 0..4 at y 0, a ramp up to y 2 over x 4..8, and a wall (vertical) that must be left out */
    arc_v3 t[] = {
        { 0, 0, 0 }, { 0, 0, 4 }, { 4, 0, 0 },   { 4, 0, 0 }, { 0, 0, 4 }, { 4, 0, 4 },
        { 4, 0, 0 }, { 4, 0, 4 }, { 8, 2, 0 },   { 8, 2, 0 }, { 4, 0, 4 }, { 8, 2, 4 },
        { 0, 0, 0 }, { 0, 3, 0 }, { 0, 0, 4 },   /* vertical */
        { 0, 0, 0 }, { 4, 0, 0 }, { 0, 0, 4 },   /* the floor again but facing down: left out */
    };
    arc_navmesh nm = arc_navmesh_create(NULL);
    arc_navmesh_add_triangles(nm, t, 18);
    CHECK(arc_navmesh_build(nm));
    CHECK(arc_navmesh_triangles(nm) == 4);
    CHECK(arc_navmesh_islands(nm) == 1);                   /* the ramp shares the floor's edge (welded) */
    bool f; float y = arc_navmesh_height(nm, arc_v3_make(6, 5, 2), &f);
    CHECK(f && fabsf(y - 1.0f) < 1e-4f);
    arc_v3 p[8];
    int n = arc_navmesh_path(nm, arc_v3_make(1, 0, 1), arc_v3_make(7, 1.5f, 2), 0, p, 8);
    CHECK(n == 2 && fabsf(p[1].y - 1.5f) < 1e-4f);
    arc_navmesh_destroy(nm);
    /* too steep (56 degrees): the ramp is left out */
    arc_v3 steep[] = { { 0, 0, 0 }, { 0, 0, 4 }, { 4, 0, 0 },   { 4, 0, 0 }, { 0, 0, 4 }, { 4, 0, 4 },
                       { 4, 0, 0 }, { 4, 0, 4 }, { 8, 6, 0 },   { 8, 6, 0 }, { 4, 0, 4 }, { 8, 6, 4 } };
    nm = arc_navmesh_create(NULL); arc_navmesh_add_triangles(nm, steep, 12); arc_navmesh_build(nm);
    CHECK(arc_navmesh_triangles(nm) == 2);
    arc_navmesh_destroy(nm);
    /* welding: two floor tiles made separately join; a gap wider than the weld does not */
    arc_v3 two[] = { { 0, 0, 0 }, { 0, 0, 1 }, { 1, 0, 0 },   { 1, 0, 0 }, { 0, 0, 1 }, { 1, 0, 1 },
                     { 1.004f, 0, 0 }, { 1.004f, 0, 1 }, { 2, 0, 0 },   { 2, 0, 0 }, { 1.004f, 0, 1 }, { 2, 0, 1 } };
    nm = arc_navmesh_create(NULL); arc_navmesh_add_triangles(nm, two, 12); arc_navmesh_build(nm);
    CHECK(arc_navmesh_islands(nm) == 1);
    arc_navmesh_destroy(nm);
    nm = arc_navmesh_create(&(arc_navmesh_desc){ .weld = 0.001f }); arc_navmesh_add_triangles(nm, two, 12); arc_navmesh_build(nm);
    CHECK(arc_navmesh_islands(nm) == 2);
    arc_navmesh_destroy(nm);
    /* from a mesh: ARC's plane (10 x 10 tiles, scaled to 20 x 20), with a box taken out */
    arc_mesh plane = arc_mesh_plane(0x808080, 10, 0x606060);
    nm = arc_navmesh_create(NULL);
    arc_navmesh_add_mesh(nm, plane, arc_m4_scale(arc_v3_make(20, 1, 20)));
    CHECK(arc_navmesh_build(nm));
    int full = arc_navmesh_triangles(nm);
    CHECK(full >= 2);
    CHECK(arc_navmesh_path(nm, arc_v3_make(-9, 0, -9), arc_v3_make(9, 0, 9), 0, p, 8) == 2);
    arc_navmesh_remove_box(nm, arc_v3_make(-1, -1, -1), arc_v3_make(1, 1, 1));
    arc_navmesh_add_mesh(nm, plane, arc_m4_scale(arc_v3_make(20, 1, 20)));   /* a new build after a build: add again */
    CHECK(arc_navmesh_build(nm));
    CHECK(full < 8 || arc_navmesh_triangles(nm) < full);
    arc_navmesh_destroy(nm);
    arc_mesh_destroy(plane);
    /* nothing walkable */
    nm = arc_navmesh_create(NULL); CHECK(!arc_navmesh_build(nm)); CHECK(arc_navmesh_path(nm, arc_v3_make(0, 0, 0), arc_v3_make(1, 0, 1), 0, p, 8) == 0); arc_navmesh_destroy(nm);
}
static void v092_run_agent(arc_navmesh nm, arc_nav_agent *a, arc_v3 target, int ticks, int *ticks_taken, bool *stayed_on) {
    arc_nav_agent_goto(a, nm, target);
    *ticks_taken = -1; *stayed_on = true;
    for (int i = 0; i < ticks; i++) {
        arc_nav_agent_update(a, nm, 1.0f / 60);
        bool f; arc_navmesh_height(nm, a->pos, &f); if (!f) *stayed_on = false;
        if (a->arrived && *ticks_taken < 0) { *ticks_taken = i + 1; break; }
    }
}
static void v092_agents(void) {
    static const char *rows[] = {
        ".....#....", ".....#....", ".....#....", ".....#....", ".....#....",
        ".....#....", ".....#....", ".....#....", "..........", "..........",
    };
    v092_grid(10, 10, rows);
    arc_navmesh nm = arc_navmesh_create(NULL);
    arc_navmesh_add_grid(nm, &v092_g, arc_v3_make(0, 0, 0), 1, NULL); arc_navmesh_build(nm);
    arc_nav_agent a; arc_nav_agent_init(&a, arc_v3_make(1.5f, 0, 1.5f), 4, 0.3f);
    int taken; bool on;
    v092_run_agent(nm, &a, arc_v3_make(8.5f, 0, 1.5f), 1200, &taken, &on);
    CHECK(a.arrived && !a.moving && on);
    CHECK(arc_v3_len(arc_v3_sub(a.pos, arc_v3_make(8.5f, 0, 1.5f))) < 0.02f);
    if (!(taken > 200 && taken < 320)) printf("ARC note v092 taken %d\n", taken);
    CHECK(taken > 200 && taken < 320);                       /* about 15 units at 4 a second, plus speeding up and slowing */
    CHECK(arc_nav_agent_remaining(&a) == 0);
    /* the same run twice gives the same bytes (deterministic) */
    arc_nav_agent b, c; arc_nav_agent_init(&b, arc_v3_make(1.5f, 0, 1.5f), 4, 0.3f); c = b;
    v092_run_agent(nm, &b, arc_v3_make(8.5f, 0, 2.5f), 150, &taken, &on);
    v092_run_agent(nm, &c, arc_v3_make(8.5f, 0, 2.5f), 150, &taken, &on);
    CHECK(memcmp(&b, &c, sizeof b) == 0);
    CHECK(b.moving && arc_nav_agent_remaining(&b) > 1);
    /* no path: failed, stays */
    arc_nav_agent d; arc_nav_agent_init(&d, arc_v3_make(1.5f, 0, 1.5f), 4, 0.6f);
    static const char *door[] = { "..#..", ".....", "..#.." };
    v092_grid(5, 3, door);
    arc_navmesh nm2 = arc_navmesh_create(NULL); arc_navmesh_add_grid(nm2, &v092_g, arc_v3_make(0, 0, 0), 1, NULL); arc_navmesh_build(nm2);
    arc_nav_agent_init(&d, arc_v3_make(0.5f, 0, 0.5f), 4, 0.6f);
    CHECK(!arc_nav_agent_goto(&d, nm2, arc_v3_make(4.5f, 0, 0.5f)) && d.failed && !d.moving);
    /* pushed into a wall: it slides along it, never into a blocked cell, and stays on the surface */
    arc_nav_agent e; arc_nav_agent_init(&e, arc_v3_make(1.5f, 0, 0.5f), 4, 0.3f);
    bool clean = true;
    for (int i = 0; i < 60; i++) {
        e.vel = arc_v3_make(4, 0, 1);   /* toward the wall cell (2, 0), a little toward the doorway row */
        arc_nav_agent_update(&e, nm2, 1.0f / 60);
        bool f; arc_navmesh_height(nm2, e.pos, &f);
        bool in_wall = e.pos.x > 2.001f && e.pos.x < 2.999f && (e.pos.z < 0.999f || e.pos.z > 2.001f);
        if (!f || in_wall) clean = false;
    }
    CHECK(clean);
    CHECK(e.pos.z > 0.99f);                                /* it slid along the wall up to the doorway row */
    arc_nav_agent e2; arc_nav_agent_init(&e2, arc_v3_make(1.5f, 0, 0.5f), 4, 0.3f);
    for (int i = 0; i < 30; i++) { e2.vel = arc_v3_make(4, 0, 0); arc_nav_agent_update(&e2, nm2, 1.0f / 60); }
    CHECK(fabsf(e2.pos.x - 2.0f) < 0.01f && fabsf(e2.pos.z - 0.5f) < 1e-3f);   /* straight at it: stops there */
    arc_navmesh_destroy(nm2);
    /* separation: two agents on one spot end up radius + radius apart */
    arc_nav_agent two[2]; arc_nav_agent_init(&two[0], arc_v3_make(2, 0, 9), 4, 0.3f); arc_nav_agent_init(&two[1], arc_v3_make(2, 0, 9), 4, 0.3f);
    arc_nav_agents_separate(two, 2, nm);
    CHECK(fabsf(arc_v3_len(arc_v3_sub(two[0].pos, two[1].pos)) - 0.6f) < 1e-3f);
    arc_navmesh_destroy(nm);
}
static void v092_long_path(void) {
    /* a zigzag: 20 walls, gaps alternating top and bottom: more corners than an agent stores (32) */
    static char rows[9][44];
    for (int y = 0; y < 9; y++) { for (int x = 0; x < 43; x++) rows[y][x] = (x % 2 == 0 && x > 0 && x < 42) ? ((x / 2) % 2 ? (y == 8 ? '.' : '#') : (y == 0 ? '.' : '#')) : '.'; rows[y][43] = 0; }
    const char *rp[9]; for (int y = 0; y < 9; y++) rp[y] = rows[y];
    v092_grid(43, 9, rp);
    arc_navmesh nm = arc_navmesh_create(NULL); arc_navmesh_add_grid(nm, &v092_g, arc_v3_make(0, 0, 0), 1, NULL); arc_navmesh_build(nm);
    CHECK(arc_navmesh_islands(nm) == 1);
    arc_v3 p[128];
    int n = arc_navmesh_path(nm, arc_v3_make(0.5f, 0, 4.5f), arc_v3_make(42.5f, 0, 4.5f), 0.2f, p, 128);
    CHECK(n > ARC_NAV_CORNERS && n <= 128);
    CHECK(v092_all_walkable(nm, p, n));
    arc_nav_agent a; arc_nav_agent_init(&a, arc_v3_make(0.5f, 0, 4.5f), 8, 0.2f);
    CHECK(arc_nav_agent_goto(&a, nm, arc_v3_make(42.5f, 0, 4.5f)) && a.partial);
    int taken; bool on;
    v092_run_agent(nm, &a, arc_v3_make(42.5f, 0, 4.5f), 60 * 60, &taken, &on);
    CHECK(a.arrived && on && !a.partial);
    CHECK(arc_v3_len(arc_v3_sub(a.pos, arc_v3_make(42.5f, 0, 4.5f))) < 0.02f);
    arc_navmesh_destroy(nm);
    free(v092_g.cells); v092_g.cells = NULL;
}
static void v092_fuzz(void) {   /* random paths across a walled level: every segment walkable, never shorter than straight */
    static const char *rows[] = {
        "........................", "..####.........#####....", "..#............#........", "..#....####....#..####..",
        "..#.......#.......#.....", "......#...#.......#.....", "......#...######..#..##.", "..#####.............#...",
        "............#.......#...", "....###.....#...#####...", "....#.......#...........", "....#...#####.....####..",
        "....#.............#.....", "........#####.....#.....", "..............#.........", "........................",
    };
    v092_grid(24, 16, rows);
    arc_navmesh nm = arc_navmesh_create(NULL); arc_navmesh_add_grid(nm, &v092_g, arc_v3_make(0, 0, 0), 1, NULL); arc_navmesh_build(nm);
    arc_rng r; arc_rng_seed(&r, 92, 3);
    int ok = 0, walk = 0, longer = 0; arc_v3 p[256];
    for (int i = 0; i < 300; i++) {
        arc_v3 a = arc_navmesh_random_point(nm, &r), b = arc_navmesh_random_point(nm, &r);
        float rad = (i % 3) * 0.15f;   /* 0, 0.15, 0.3 */
        int n = arc_navmesh_path(nm, a, b, rad, p, 256);
        if (n >= 2) ok++;
        bool w = true; for (int k = 1; k < n && rad > 0; k++) if (!arc_navmesh_walkable_line(nm, p[k - 1], p[k], NULL)) w = false;
        walk += w;
        longer += arc_navmesh_path_length(p, n) >= arc_v3_len(arc_v3_sub(b, a)) - 1e-3f;
    }
    CHECK(ok == 300);       /* one island: always a path (these doorways are all at least 1 wide) */
    CHECK(walk == 300);
    CHECK(longer == 300);
    arc_navmesh_destroy(nm);
}
static void v092_init(void) { v092_fuzz(); v092_paths(); v092_islands_and_width(); v092_soup_and_meshes(); v092_agents(); v092_long_path(); }
