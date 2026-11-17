/* ARC 0.9.4 self-test: deterministic rigid bodies. Included by test_core.c; v094_init() runs in init. Names start
   with v094_ (one translation unit). */
#define V094_DT (1.0f / 60)
static void v094_run(arc_physics w, float seconds) { int n = (int)(seconds * 60 + 0.5f); for (int i = 0; i < n; i++) arc_physics_step(w, V094_DT); }
static arc_physics v094_ground(void) { arc_physics w = arc_physics_create(NULL); arc_physics_add_plane(w, arc_v3_make(0, 1, 0), 0); return w; }
static float v094_up_dot(arc_physics w, int b, int axis) {   /* how upright a body's axis is (1 = straight up or down) */
    arc_m4 m = arc_physics_matrix(w, b); return fabsf(m.m[axis * 4 + 1]);
}
static void v094_rest(void) {
    /* a sphere falls and comes to rest on the ground, then sleeps */
    arc_physics w = v094_ground();
    int s = arc_physics_add(w, &(arc_body_desc){ .shape = ARC_SHAPE_SPHERE, .size = { 0.5f }, .pos = { 0, 3, 0 } });
    CHECK(s == 1 && arc_physics_body_count(w) == 1);
    v094_run(w, 0.5f);
    CHECK(arc_physics_pos(w, s).y < 2.0f);                         /* it fell */
    v094_run(w, 3);
    CHECK(fabsf(arc_physics_pos(w, s).y - 0.5f) < 0.01f);
    CHECK(arc_physics_sleeping(w, s));
    /* a box lands flat and rests at its half height */
    int b = arc_physics_add(w, &(arc_body_desc){ .shape = ARC_SHAPE_BOX, .size = { 0.5f, 0.25f, 0.4f }, .pos = { 3, 2, 0 }, .mass = 5 });
    v094_run(w, 3);
    CHECK(fabsf(arc_physics_pos(w, b).y - 0.25f) < 0.01f);
    CHECK(v094_up_dot(w, b, 1) > 0.999f);
    CHECK(arc_physics_sleeping(w, b));
    /* a capsule lying down rests at its radius */
    int c = arc_physics_add(w, &(arc_body_desc){ .shape = ARC_SHAPE_CAPSULE, .size = { 0.3f, 0.5f }, .pos = { -3, 1, 0 }, .rot = arc_quat_axis_angle(arc_v3_make(0, 0, 1), 1.5707963f) });
    v094_run(w, 3);
    CHECK(fabsf(arc_physics_pos(w, c).y - 0.3f) < 0.01f);
    /* a tumbling box ends on one of its faces */
    int t = arc_physics_add(w, &(arc_body_desc){ .shape = ARC_SHAPE_BOX, .size = { 0.3f, 0.3f, 0.3f }, .pos = { 6, 2, 0 }, .rot = arc_quat_axis_angle(arc_v3_norm(arc_v3_make(1, 2, 3)), 0.9f), .ang_vel = { 3, 1, 2 } });
    v094_run(w, 5);
    CHECK(fabsf(arc_physics_pos(w, t).y - 0.3f) < 0.01f);
    float best = fmaxf(v094_up_dot(w, t, 0), fmaxf(v094_up_dot(w, t, 1), v094_up_dot(w, t, 2)));
    CHECK(best > 0.999f);
    arc_physics_destroy(w);
}
static void v094_stack(void) {
    /* a stack of six boxes stands, then sleeps */
    arc_physics w = v094_ground();
    int b[6];
    for (int i = 0; i < 6; i++) b[i] = arc_physics_add(w, &(arc_body_desc){ .shape = ARC_SHAPE_BOX, .size = { 0.5f, 0.5f, 0.5f }, .pos = { 0, 0.5f + (float)i * 1.0f, 0 }, .mass = 10 });
    v094_run(w, 8);
    arc_v3 top = arc_physics_pos(w, b[5]);
    CHECK(fabsf(top.x) < 0.05f && fabsf(top.z) < 0.05f);
    CHECK(fabsf(top.y - 5.5f) < 0.05f);
    bool asleep = true; for (int i = 0; i < 6; i++) asleep = asleep && arc_physics_sleeping(w, b[i]);
    CHECK(asleep);
    /* knock the top one: it wakes, and only things it touches wake */
    arc_physics_impulse(w, b[5], arc_v3_make(60, 0, 0), arc_physics_pos(w, b[5]));
    CHECK(!arc_physics_sleeping(w, b[5]));
    v094_run(w, 3);
    CHECK(arc_physics_pos(w, b[5]).y < 1.0f || fabsf(arc_physics_pos(w, b[5]).x) > 0.9f);   /* it went off the top */
    CHECK(fabsf(arc_physics_pos(w, b[0]).x) < 0.05f);              /* the bottom stayed */
    arc_physics_destroy(w);
}
static void v094_bounce_slide(void) {
    /* restitution 0.8 from 2 m: the first bounce peaks near 0.64 x 2 = 1.28 m (radius 0.25) */
    arc_physics w = v094_ground();
    int s = arc_physics_add(w, &(arc_body_desc){ .shape = ARC_SHAPE_SPHERE, .size = { 0.25f }, .pos = { 0, 2.25f, 0 }, .restitution = 0.8f });
    float peak = 0; bool landed = false;
    for (int i = 0; i < 180; i++) {
        arc_physics_step(w, V094_DT);
        float y = arc_physics_pos(w, s).y - 0.25f;
        if (!landed && y < 0.02f) landed = true;
        if (landed && y > peak) peak = y;
    }
    CHECK(peak > 1.1f && peak < 1.45f);
    arc_physics_destroy(w);
    /* friction: a box sliding at 5 m/s stops (friction 0.5: about 2.5 m); with friction ~0 it keeps going */
    w = v094_ground();
    int a = arc_physics_add(w, &(arc_body_desc){ .shape = ARC_SHAPE_BOX, .size = { 0.5f, 0.5f, 0.5f }, .pos = { 0, 0.5f, 0 }, .vel = { 5, 0, 0 } });
    int z = arc_physics_add(w, &(arc_body_desc){ .shape = ARC_SHAPE_BOX, .size = { 0.5f, 0.5f, 0.5f }, .pos = { 0, 0.5f, 5 }, .vel = { 5, 0, 0 }, .friction = 1e-4f, .linear_damping = 1e-4f });
    v094_run(w, 3);
    float da = arc_physics_pos(w, a).x;
    CHECK(da > 1.5f && da < 4.0f && arc_physics_vel(w, a).x < 0.05f);
    CHECK(arc_physics_pos(w, z).x > 13.0f);
    arc_physics_destroy(w);
    /* a slope of 20 degrees: friction 0.8 holds a box (tan 20 = 0.36), friction 0.1 lets it slide */
    w = arc_physics_create(NULL);
    float ang = 0.349066f, c = 0.9396926f, sn = 0.3420201f;   /* 20 degrees */
    (void)ang;
    arc_physics_add_plane(w, arc_v3_make(-sn, c, 0), 0);          /* rising toward +x */
    int hold = arc_physics_add(w, &(arc_body_desc){ .shape = ARC_SHAPE_BOX, .size = { 0.4f, 0.4f, 0.4f }, .pos = { 0, 0.4f / c + 0.01f, 0 }, .rot = arc_quat_axis_angle(arc_v3_make(0, 0, 1), 0.349066f), .friction = 0.8f });
    int slide = arc_physics_add(w, &(arc_body_desc){ .shape = ARC_SHAPE_BOX, .size = { 0.4f, 0.4f, 0.4f }, .pos = { 0, 0.4f / c + 0.01f, 3 }, .rot = arc_quat_axis_angle(arc_v3_make(0, 0, 1), 0.349066f), .friction = 0.1f });
    v094_run(w, 2);
    CHECK(fabsf(arc_physics_pos(w, hold).x) < 0.05f);
    CHECK(arc_physics_pos(w, slide).x < -2.0f);
    arc_physics_destroy(w);
}
static arc_v3 v094_on_seg(arc_v3 p, arc_v3 a, arc_v3 b) {
    arc_v3 ab = arc_v3_sub(b, a); float l2 = arc_v3_dot(ab, ab); if (l2 < 1e-12f) return a;
    return arc_v3_add(a, arc_v3_scale(ab, arc_clampf(arc_v3_dot(arc_v3_sub(p, a), ab) / l2, 0, 1)));
}
static void v094_seg_seg(arc_v3 p1, arc_v3 q1, arc_v3 p2, arc_v3 q2, arc_v3 *c1, arc_v3 *c2) {   /* by refinement: fine for a test */
    arc_v3 a = p1, b = p2;
    for (int i = 0; i < 40; i++) { b = v094_on_seg(a, p2, q2); a = v094_on_seg(b, p1, q1); }
    *c1 = a; *c2 = b;
}
static void v094_collisions(void) {
    /* equal spheres head on, restitution 1, no gravity: they swap velocities */
    arc_physics w = arc_physics_create(&(arc_physics_desc){ .no_gravity = true, .no_sleep = true });
    int a = arc_physics_add(w, &(arc_body_desc){ .shape = ARC_SHAPE_SPHERE, .size = { 0.5f }, .pos = { -2, 0, 0 }, .vel = { 3, 0, 0 }, .restitution = 1, .linear_damping = 1e-6f });
    int b = arc_physics_add(w, &(arc_body_desc){ .shape = ARC_SHAPE_SPHERE, .size = { 0.5f }, .pos = { 2, 0, 0 }, .vel = { -3, 0, 0 }, .restitution = 1, .linear_damping = 1e-6f });
    v094_run(w, 2);
    CHECK(fabsf(arc_physics_vel(w, a).x + 3) < 0.15f && fabsf(arc_physics_vel(w, b).x - 3) < 0.15f);
    CHECK(fabsf(arc_physics_vel(w, a).x + arc_physics_vel(w, b).x) < 1e-3f);   /* momentum kept */
    /* events: the hit is reported once, with an impulse near m * dv = 523.6 * 6 */
    arc_physics_destroy(w);
    w = v094_ground();
    int crate = arc_physics_add(w, &(arc_body_desc){ .shape = ARC_SHAPE_BOX, .size = { 0.5f, 0.5f, 0.5f }, .pos = { 0, 2, 0 }, .mass = 10 });
    int hits = 0; float J = 0;
    for (int i = 0; i < 120; i++) {
        arc_physics_step(w, V094_DT);
        arc_physics_event ev[8]; int n = arc_physics_events(w, ev, 8);
        for (int k = 0; k < n; k++) if (ev[k].a == 0 && ev[k].b == crate) { hits++; J += ev[k].impulse; }
    }
    CHECK(hits == 1);
    CHECK(J > 40 && J < 75);                                       /* 10 kg landing at about 5.4 m/s */
    /* a sphere rests on the crate; a capsule lies on a box */
    int s1 = arc_physics_add(w, &(arc_body_desc){ .shape = ARC_SHAPE_SPHERE, .size = { 0.3f }, .pos = { 0, 2.5f, 0 } });
    arc_physics_add(w, &(arc_body_desc){ .shape = ARC_SHAPE_BOX, .size = { 1, 0.25f, 1 }, .pos = { 4, 0.25f, 0 }, .mass = 50 });
    int cap = arc_physics_add(w, &(arc_body_desc){ .shape = ARC_SHAPE_CAPSULE, .size = { 0.25f, 0.4f }, .pos = { 4, 1.5f, 0 }, .rot = arc_quat_axis_angle(arc_v3_make(1, 0, 0), 1.5707963f) });
    /* a capsule dropped across it, and a sphere: round on round rolls off, but they must never pass through each other */
    int cap2 = arc_physics_add(w, &(arc_body_desc){ .shape = ARC_SHAPE_CAPSULE, .size = { 0.2f, 0.4f }, .pos = { 4, 2.5f, 0 }, .rot = arc_quat_axis_angle(arc_v3_make(0, 0, 1), 1.5707963f) });
    int s2 = arc_physics_add(w, &(arc_body_desc){ .shape = ARC_SHAPE_SPHERE, .size = { 0.2f }, .pos = { 4.05f, 3.5f, 0.1f } });
    float worst = 1;
    for (int i = 0; i < 240; i++) {
        arc_physics_step(w, V094_DT);
        arc_m4 m1 = arc_physics_matrix(w, cap), m2 = arc_physics_matrix(w, cap2);
        arc_v3 a1 = arc_physics_pos(w, cap), a2 = arc_physics_pos(w, cap2), d1 = arc_v3_make(m1.m[4], m1.m[5], m1.m[6]), d2 = arc_v3_make(m2.m[4], m2.m[5], m2.m[6]);
        arc_v3 c1, c2; v094_seg_seg(arc_v3_sub(a1, arc_v3_scale(d1, 0.4f)), arc_v3_add(a1, arc_v3_scale(d1, 0.4f)), arc_v3_sub(a2, arc_v3_scale(d2, 0.4f)), arc_v3_add(a2, arc_v3_scale(d2, 0.4f)), &c1, &c2);
        worst = fminf(worst, arc_v3_len(arc_v3_sub(c1, c2)) - 0.45f);                     /* capsule to capsule */
        arc_v3 q = v094_on_seg(arc_physics_pos(w, s2), arc_v3_sub(a1, arc_v3_scale(d1, 0.4f)), arc_v3_add(a1, arc_v3_scale(d1, 0.4f)));
        worst = fminf(worst, arc_v3_len(arc_v3_sub(q, arc_physics_pos(w, s2))) - 0.45f);   /* sphere to capsule */
    }
    if (!(worst > -0.03f)) printf("ARC note v094 worst overlap %.4f\n", worst);
    CHECK(worst > -0.03f);
    CHECK(fabsf(arc_physics_pos(w, s1).y - 1.3f) < 0.02f);          /* on the crate: 1.0 + 0.3 */
    CHECK(fabsf(arc_physics_pos(w, cap).y - 0.75f) < 0.03f);        /* on the base: 0.5 + 0.25 */
    CHECK(arc_physics_pos(w, cap2).y > 0.15f && arc_physics_pos(w, s2).y > 0.15f);   /* not through the ground */
    arc_physics_destroy(w);
}
static void v094_mesh_kinematic_masks(void) {
    /* a ramp made of triangles (a plane mesh tilted 15 degrees): a ball rolls down it */
    arc_physics w = arc_physics_create(NULL);
    arc_mesh plane = arc_mesh_plane(0x808080, 4, 0x606060);
    arc_physics_add_mesh(w, plane, arc_m4_mul(arc_m4_rotate_z(0.2618f), arc_m4_scale(arc_v3_make(20, 1, 6))));
    arc_mesh_destroy(plane);
    int ball = arc_physics_add(w, &(arc_body_desc){ .shape = ARC_SHAPE_SPHERE, .size = { 0.3f }, .pos = { 5, 1.34f + 0.35f, 0 } });
    v094_run(w, 1.5f);
    arc_v3 p = arc_physics_pos(w, ball);
    /* rolling without slipping: a = 5/7 g sin(15 deg) = 1.81 m/s2 along the slope: about 2 m in 1.5 s */
    float along = (5 - p.x) / cosf(0.2618f);
    CHECK(along > 1.6f && along < 2.2f);
    float surf = tanf(0.2618f) * p.x;
    CHECK(p.y > surf + 0.25f && p.y < surf + 0.36f);                /* on the surface, not through it */
    /* a box rests on triangle ground */
    int box = arc_physics_add(w, &(arc_body_desc){ .shape = ARC_SHAPE_BOX, .size = { 0.3f, 0.3f, 0.3f }, .pos = { 0, 1.5f, 2 }, .rot = arc_quat_axis_angle(arc_v3_make(0, 0, 1), 0.2618f), .friction = 1 });
    v094_run(w, 2);
    arc_v3 bp = arc_physics_pos(w, box);
    CHECK(bp.y > tanf(0.2618f) * bp.x + 0.25f && bp.y < tanf(0.2618f) * bp.x + 0.36f);
    arc_physics_destroy(w);
    /* a kinematic paddle pushes a sphere; layers and masks let a ghost pass through */
    w = v094_ground();
    int pad = arc_physics_add(w, &(arc_body_desc){ .type = ARC_BODY_KINEMATIC, .shape = ARC_SHAPE_BOX, .size = { 0.2f, 0.5f, 1 }, .pos = { -2, 0.5f, 0 } });
    int ball2 = arc_physics_add(w, &(arc_body_desc){ .shape = ARC_SHAPE_SPHERE, .size = { 0.3f }, .pos = { 0, 0.3f, 0 } });
    int ghost = arc_physics_add(w, &(arc_body_desc){ .shape = ARC_SHAPE_SPHERE, .size = { 0.3f }, .pos = { 0, 0.3f, 0.5f }, .layer = 2, .mask = 0xFFFFFFFEu });
    for (int i = 0; i < 60; i++) { arc_physics_move_kinematic(w, pad, arc_v3_make(-2 + (float)i * 0.05f, 0.5f, 0), (arc_quat){ 0 }); arc_physics_step(w, V094_DT); }
    CHECK(arc_physics_pos(w, ball2).x > 1.2f);                      /* pushed ahead of the paddle */
    CHECK(fabsf(arc_physics_pos(w, ghost).x) < 0.05f);              /* the paddle (layer 1) is masked out for it */
    /* raycast: hits the nearest body, then the ground */
    arc_physics_hit hit;
    CHECK(arc_physics_raycast(w, arc_v3_make(arc_physics_pos(w, ball2).x, 5, 0), arc_v3_make(0, -1, 0), 100, 0, &hit));
    CHECK(hit.body == ball2 && fabsf(hit.t - (5 - 0.6f)) < 0.02f && hit.normal.y > 0.99f);
    CHECK(arc_physics_raycast(w, arc_v3_make(-5, 5, 5), arc_v3_make(0, -1, 0), 100, 0, &hit) && hit.body == 0 && fabsf(hit.t - 5) < 1e-4f);
    CHECK(!arc_physics_raycast(w, arc_v3_make(-5, 5, 5), arc_v3_make(0, 1, 0), 100, 0, &hit));
    int over[4];
    CHECK(arc_physics_overlap_sphere(w, arc_physics_pos(w, ball2), 0.1f, 0, over, 4) == 1 && over[0] == ball2);
    /* remove */
    arc_physics_remove(w, ghost);
    CHECK(arc_physics_body_count(w) == 2 && arc_physics_pos(w, ghost).x == 0);
    arc_physics_destroy(w);
}
static arc_physics v094_scene(void) {   /* a pile of mixed shapes */
    arc_physics w = v094_ground();
    arc_rng r; arc_rng_seed(&r, 94, 1);
    for (int i = 0; i < 40; i++) {
        arc_body_desc d = { .shape = i % 3, .pos = { (float)(i % 8) - 3.5f + arc_rng_float(&r) * 0.4f, 1 + (float)(i / 8) * 0.9f, arc_rng_float(&r) * 2 - 1 } };
        d.size = d.shape == ARC_SHAPE_BOX ? arc_v3_make(0.2f + arc_rng_float(&r) * 0.3f, 0.2f + arc_rng_float(&r) * 0.3f, 0.2f + arc_rng_float(&r) * 0.3f)
                                          : arc_v3_make(0.15f + arc_rng_float(&r) * 0.2f, 0.2f + arc_rng_float(&r) * 0.2f, 0);
        d.rot = arc_quat_axis_angle(arc_v3_norm(arc_v3_make(arc_rng_float(&r) - 0.5f, arc_rng_float(&r) - 0.5f, arc_rng_float(&r) - 0.5f)), arc_rng_float(&r) * 3);
        arc_physics_add(w, &d);
    }
    return w;
}
static void v094_determinism(void) {
    arc_physics a = v094_scene(), b = v094_scene();
    v094_run(a, 2); v094_run(b, 2);
    size_t n = arc_physics_save(a, NULL, 0);
    CHECK(n > 0 && n == arc_physics_save(b, NULL, 0));
    uint8_t *sa = (uint8_t *)malloc(n), *sb = (uint8_t *)malloc(n), *mid = NULL;
    arc_physics_save(a, sa, n); arc_physics_save(b, sb, n);
    CHECK(memcmp(sa, sb, n) == 0);                                   /* two runs, the same bytes */
    /* save, run on, load, run the same again: the same bytes */
    size_t nm = arc_physics_save(a, NULL, 0); mid = (uint8_t *)malloc(nm); arc_physics_save(a, mid, nm);
    v094_run(a, 1.5f);
    size_t n1 = arc_physics_save(a, NULL, 0); uint8_t *s1 = (uint8_t *)malloc(n1); arc_physics_save(a, s1, n1);
    CHECK(arc_physics_load(a, mid, nm));
    v094_run(a, 1.5f);
    size_t n2 = arc_physics_save(a, NULL, 0); uint8_t *s2 = (uint8_t *)malloc(n2); arc_physics_save(a, s2, n2);
    CHECK(n1 == n2 && memcmp(s1, s2, n1) == 0);
    CHECK(!arc_physics_load(a, mid, nm - 1));                        /* a wrong size is refused */
    /* everything ends up at rest on the ground, nothing below it */
    v094_run(a, 14);
    int asleep = 0, below = 0;
    for (int i = 1; i <= 40; i++) { asleep += arc_physics_sleeping(a, i); below += arc_physics_pos(a, i).y < 0.1f; }
    CHECK(asleep >= 38);
    CHECK(below == 0);
    free(sa); free(sb); free(mid); free(s1); free(s2);
    arc_physics_destroy(a); arc_physics_destroy(b);
}
static void v094_init(void) { v094_rest(); v094_stack(); v094_bounce_slide(); v094_collisions(); v094_mesh_kinematic_masks(); v094_determinism(); }
