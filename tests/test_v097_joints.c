/* ARC 0.9.7 self-test: physics joints (hinge, ball). Included by test_core.c; v097j_init() runs in init. Names start
   with v097j_ (one translation unit). */
#define V097J_DT (1.0f / 60)
static void v097j_run(arc_physics w, float seconds) { int n = (int)(seconds * 60 + 0.5f); for (int i = 0; i < n; i++) arc_physics_step(w, V097J_DT); }
static void v097j_pendulum(void) {
    /* a small ball on a hinge 1 m from a world pivot, let go 10 degrees out: period 2 pi sqrt(I / (m g d)) with
       I = 0.4 m r^2 + m d^2 (2.0135 s with the small-swing correction); the pivot holds; the angle reads the swing */
    arc_physics w = arc_physics_create(&(arc_physics_desc){ .no_sleep = true });
    float s = 0.17364818f, c = 0.98480775f;   /* sin, cos of 10 degrees */
    int b = arc_physics_add(w, &(arc_body_desc){ .shape = ARC_SHAPE_SPHERE, .size = { 0.1f }, .pos = { s, -c, 0 }, .mass = 1, .linear_damping = 1e-6f, .angular_damping = 1e-6f });
    int j = arc_physics_add_joint(w, &(arc_joint_desc){ .type = ARC_JOINT_HINGE, .a = b, .b = 0, .anchor = { 0, 0, 0 }, .axis = { 0, 0, 1 } });
    CHECK(j == 1 && arc_physics_joint_count(w) == 1 && fabsf(arc_physics_joint_angle(w, j)) < 1e-5f);
    float prevx = s, t = 0, cross[4]; int nc = 0; float worst = 0, amax = 0;
    for (int i = 0; i < 60 * 7 && nc < 3; i++) {
        arc_physics_step(w, V097J_DT); t += V097J_DT;
        arc_v3 p = arc_physics_pos(w, b); worst = fmaxf(worst, fabsf(arc_v3_len(p) - 1));
        amax = fmaxf(amax, fabsf(arc_physics_joint_angle(w, j)));
        if (prevx > 0 && p.x <= 0) cross[nc++] = t - V097J_DT * p.x / (p.x - prevx);   /* through the bottom, moving -x */
        prevx = p.x;
    }
    float period = nc >= 3 ? (cross[2] - cross[0]) / 2 : 0;
    printf("ARC note pendulum: period %.4f s (expected 2.0135), pivot drift %.5f m, largest angle %.4f rad\n", period, worst, amax);
    CHECK(fabsf(period - 2.0135f) < 0.02f);
    CHECK(worst < 0.005f);
    CHECK(fabsf(amax - 0.349f) < 0.01f);   /* swings through to 10 degrees on the other side: 20 degrees from the start */
    arc_physics_destroy(w);
}
static void v097j_door_motor(void) {
    arc_physics w = arc_physics_create(NULL); arc_physics_add_plane(w, arc_v3_make(0, 1, 0), 0);
    /* a door on a vertical hinge with limits -0.5 .. 1.2 rad, kicked open hard: it stops at the limit, swings back */
    int d = arc_physics_add(w, &(arc_body_desc){ .shape = ARC_SHAPE_BOX, .size = { 0.5f, 1, 0.05f }, .pos = { 0.5f, 1.05f, 0 }, .mass = 20 });
    int jd = arc_physics_add_joint(w, &(arc_joint_desc){ .a = d, .anchor = { 0, 1.05f, 0 }, .axis = { 0, 1, 0 }, .limit = true, .lower = -0.5f, .upper = 1.2f });
    arc_physics_set_vel(w, d, arc_v3_make(0, 0, -4), arc_v3_make(0, 8, 0));
    float amax = -9, amin = 9;
    for (int i = 0; i < 180; i++) { arc_physics_step(w, V097J_DT); float a = arc_physics_joint_angle(w, jd); amax = fmaxf(amax, a); amin = fminf(amin, a); }
    printf("ARC note door: angle range %.3f .. %.3f (limits -0.5 .. 1.2)\n", amin, amax);
    CHECK(amax <= 1.2f + 0.03f && amax > 1.1f);
    CHECK(amin >= -0.5f - 0.03f);
    arc_v3 dp = arc_physics_pos(w, d); CHECK(fabsf(dp.y - 1.05f) < 0.01f);   /* held up by its hinge, off the floor */
    /* a wheel on a horizontal axle with a motor: reaches its speed; a brake (speed 0) stops it; the axle stays put */
    int wh = arc_physics_add(w, &(arc_body_desc){ .shape = ARC_SHAPE_SPHERE, .size = { 0.3f }, .pos = { 5, 1, 0 }, .mass = 5 });
    int jw = arc_physics_add_joint(w, &(arc_joint_desc){ .a = wh, .anchor = { 5, 1, 0 }, .axis = { 1, 0, 0 }, .motor = true, .motor_speed = 3, .motor_torque = 20 });
    v097j_run(w, 1.5f);
    float spin = arc_v3_dot(arc_physics_ang_vel(w, wh), arc_v3_make(1, 0, 0));
    printf("ARC note motor: spin %.3f rad/s (target 3)\n", spin);
    CHECK(fabsf(spin - 3) < 0.05f);
    arc_physics_joint_motor(w, jw, 0, 50); v097j_run(w, 1);
    CHECK(fabsf(arc_v3_dot(arc_physics_ang_vel(w, wh), arc_v3_make(1, 0, 0))) < 0.02f);
    CHECK(arc_v3_dist(arc_physics_pos(w, wh), arc_v3_make(5, 1, 0)) < 0.005f);
    /* limits set later; a removed joint lets go */
    arc_physics_joint_limits(w, jd, true, -0.1f, 0.1f); v097j_run(w, 2);
    CHECK(fabsf(arc_physics_joint_angle(w, jd)) < 0.13f);
    arc_physics_remove_joint(w, jd); v097j_run(w, 2);
    CHECK(arc_physics_joint_count(w) == 1 && arc_physics_pos(w, d).y < 1.03f);   /* let go: down onto the floor (it hung 5 cm up) */
    arc_physics_destroy(w);
}
static void v097j_rope_and_pairs(void) {
    /* a rope: 10 capsules end to end on ball joints, hung from the world, starting sideways: it swings down, hangs
       straight under the pivot at its full length, and the links stay joined */
    arc_physics w = arc_physics_create(NULL); arc_physics_add_plane(w, arc_v3_make(0, 1, 0), -20);
    int link[10]; arc_quat side = { 0, 0, 0.70710678f, 0.70710678f };   /* capsule's long axis (y) turned to x */
    for (int i = 0; i < 10; i++) link[i] = arc_physics_add(w, &(arc_body_desc){ .shape = ARC_SHAPE_CAPSULE, .size = { 0.05f, 0.2f }, .pos = { 0.25f + 0.5f * (float)i, 10, 0 }, .rot = side, .mass = 0.5f, .angular_damping = 1.5f, .linear_damping = 1.0f });
    arc_physics_add_joint(w, &(arc_joint_desc){ .type = ARC_JOINT_BALL, .a = link[0], .b = 0, .anchor = { 0, 10, 0 } });
    for (int i = 1; i < 10; i++) arc_physics_add_joint(w, &(arc_joint_desc){ .type = ARC_JOINT_BALL, .a = link[i - 1], .b = link[i], .anchor = { 0.5f * (float)i, 10, 0 } });
    v097j_run(w, 20);
    arc_v3 end = arc_physics_pos(w, link[9]);
    float gap = 0; for (int i = 1; i < 10; i++) gap = fmaxf(gap, fabsf(arc_v3_dist(arc_physics_pos(w, link[i - 1]), arc_physics_pos(w, link[i])) - 0.5f));
    printf("ARC note rope: last link at %.3f %.3f %.3f (straight down: 0, 5.25, 0), worst link gap error %.4f m, asleep %d\n", end.x, end.y, end.z, gap, arc_physics_sleeping(w, link[9]));
    CHECK(fabsf(end.x) < 0.08f && fabsf(end.y - 5.25f) < 0.06f && fabsf(end.z) < 0.05f);
    CHECK(gap < 0.01f);
    CHECK(arc_physics_sleeping(w, link[9]));
    /* joined bodies don't collide unless asked: two overlapping boxes on a ball joint stay overlapped (no push) */
    int p = arc_physics_add(w, &(arc_body_desc){ .shape = ARC_SHAPE_BOX, .size = { 0.5f, 0.5f, 0.5f }, .pos = { 20, 0, 0 } });
    int q = arc_physics_add(w, &(arc_body_desc){ .shape = ARC_SHAPE_BOX, .size = { 0.5f, 0.5f, 0.5f }, .pos = { 20.4f, 0, 0 } });
    arc_physics_add_joint(w, &(arc_joint_desc){ .type = ARC_JOINT_BALL, .a = p, .b = q, .anchor = { 20.2f, 0, 0 } });
    arc_physics_add_joint(w, &(arc_joint_desc){ .type = ARC_JOINT_BALL, .a = p, .b = 0, .anchor = { 20, 0, 0 } });
    v097j_run(w, 0.5f);
    CHECK(arc_v3_dist(arc_physics_pos(w, p), arc_physics_pos(w, q)) < 0.45f);
    /* save, run, load, run again: the same bytes (joints and their warm-start impulses included) */
    arc_physics_set_vel(w, link[9], arc_v3_make(3, 0, 1), arc_v3_make(0, 0, 0));
    size_t n = arc_physics_save(w, NULL, 0); uint8_t *s0 = (uint8_t *)malloc(n), *s1 = (uint8_t *)malloc(n), *s2 = (uint8_t *)malloc(n);
    arc_physics_save(w, s0, n); v097j_run(w, 2); arc_physics_save(w, s1, n);
    CHECK(arc_physics_load(w, s0, n)); v097j_run(w, 2); arc_physics_save(w, s2, n);
    CHECK(!memcmp(s1, s2, n));
    free(s0); free(s1); free(s2);
    /* removing a body removes its joints; bad input is refused */
    int before = arc_physics_joint_count(w); arc_physics_remove(w, link[5]);
    CHECK(arc_physics_joint_count(w) == before - 2);
    CHECK(arc_physics_add_joint(w, &(arc_joint_desc){ .a = link[0], .b = link[0] }) == 0 && arc_physics_add_joint(w, &(arc_joint_desc){ .a = 999 }) == 0);
    CHECK(arc_physics_add_joint(w, &(arc_joint_desc){ .type = 7, .a = link[0] }) == 0);
    arc_physics_destroy(w);
}
/* ---- 0.9.7 terrain chunk culling (in the same file: the 0.9.7 self-tests) ---- */
static void v097c_culling(void) {
    static float h[129 * 129];
    for (int j = 0; j < 129; j++) for (int i = 0; i < 129; i++) h[j * 129 + i] = 4 * arc_noise_fbm2((float)i * 0.05f, (float)j * 0.05f, 3, 2u);
    arc_terrain t = arc_terrain_create(&(arc_terrain_desc){ .heights = h, .samples_x = 129, .samples_z = 129, .size_x = 128, .size_z = 128, .origin = { -64, 0, -64 }, .chunk = 16 });
    CHECK(arc_terrain_chunk_count(t) == 64);
    arc_set_shadows(arc_v3_make(0, 0, 0), 0, 0);
    /* low in the middle looking along +x, a 50 degree view: about a quarter of the chunks */
    arc_camera cam = { { 0, 6, 0 }, { 10, 4, 0 }, { 0, 1, 0 }, 0.87f, 0, 0.1f, 300 }; arc_set_camera(&cam);
    arc_terrain_draw(t, 0xFFFFFF); int ahead = arc_terrain_drawn(t);
    /* high above looking down at all of it: every chunk */
    arc_camera top = { { 0, 200, 0.01f }, { 0, 0, 0 }, { 0, 1, 0 }, 1.2f, 0, 0.1f, 500 }; arc_set_camera(&top);
    arc_terrain_draw(t, 0xFFFFFF); int all = arc_terrain_drawn(t);
    /* culling off: every chunk whatever the view */
    arc_set_camera(&cam); arc_terrain_set_culling(t, false); arc_terrain_draw(t, 0xFFFFFF); int off = arc_terrain_drawn(t); arc_terrain_set_culling(t, true);
    /* sun shadows from low in the east (+x, ahead of the camera): chunks further ahead stay for their shadows; a sun
       behind the camera adds nothing in front */
    arc_set_light(arc_v3_make(1, 0.3f, 0), 0xFFFFFF, 0x202020); arc_set_shadows(arc_v3_make(10, 0, 0), 30, 1024);
    arc_terrain_draw(t, 0xFFFFFF); int sun_ahead = arc_terrain_drawn(t);
    arc_set_light(arc_v3_make(-1, 0.3f, 0), 0xFFFFFF, 0x202020); arc_terrain_draw(t, 0xFFFFFF); int sun_behind = arc_terrain_drawn(t);
    /* a reflection plane: every chunk */
    arc_reflection_plane(0, 0.5f, 0.5f); arc_terrain_draw(t, 0xFFFFFF); int mirror = arc_terrain_drawn(t); arc_reflection_plane(0, 0, 0);
    printf("ARC note terrain culling: looking along the ground %d of 64, from above %d, culling off %d, low sun ahead %d, behind %d, with a mirror %d\n", ahead, all, off, sun_ahead, sun_behind, mirror);
    CHECK(ahead >= 8 && ahead <= 28 && all == 64 && off == 64 && mirror == 64);
    CHECK(sun_ahead >= ahead && sun_behind >= ahead);
    arc_set_shadows(arc_v3_make(0, 0, 0), 0, 0); arc_set_light(arc_v3_make(0.4f, 1.0f, 0.3f), 0xFFFFFF, 0x404850);
    arc_terrain_destroy(t);
}
static void v097j_init(void) { v097j_pendulum(); v097j_door_motor(); v097j_rope_and_pairs(); v097c_culling(); }
