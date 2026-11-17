/* ARC 0.9.3 self-test: animation graphs (state machines, blends, events) and two-bone IK. Included by test_core.c;
   v093_init() runs in init. Names start with v093_ (one translation unit). */
static uint8_t v093_buf[16384]; static size_t v093_n;
static uint8_t *v093_u32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); return p + 4; }
static uint8_t *v093_f(uint8_t *p, float v) { memcpy(p, &v, 4); return p + 4; }
/* rig: 0 hips at (0,2,0), 1 thigh (at the hips), 2 knee 1 below, 3 ankle 1 below, 4 toe 0.3 forward (+z).
   clips move only the hips: idle (still), walk (x 0 -> 1 -> 0 in 1 s, loops), run (x 0 -> 2 -> 0 in 0.5 s, loops),
   jump (y 2 -> 3 in 0.5 s, once) */
static const float v093_rest[5][3] = { { 0, 2, 0 }, { 0, 0, 0 }, { 0, -1, 0 }, { 0, -1, 0 }, { 0, 0, 0.3f } };
static uint8_t *v093_trs(uint8_t *p, float x, float y, float z) {
    float v[10] = { x, y, z, 0, 0, 0, 1, 1, 1, 1 }; for (int i = 0; i < 10; i++) p = v093_f(p, v[i]); return p;
}
static uint8_t *v093_clip(uint8_t *p, const char *name, float fps, int frames, bool loop, const float (*hips)[3]) {
    memset(p, 0, 32); strcpy((char *)p, name); p += 32; p = v093_f(p, fps); p = v093_u32(p, (uint32_t)frames); p = v093_u32(p, loop ? 1 : 0);
    for (int f = 0; f < frames; f++) for (int j = 0; j < 5; j++)
        p = j == 0 ? v093_trs(p, hips[f][0], hips[f][1], hips[f][2]) : v093_trs(p, v093_rest[j][0], v093_rest[j][1], v093_rest[j][2]);
    return p;
}
static arc_anim v093_rig(void) {
    static const char *names[5] = { "hips", "thigh", "knee", "ankle", "toe" };
    uint8_t *p = v093_buf; memcpy(p, "ARCA", 4); p += 4; p = v093_u32(p, 1); p = v093_u32(p, 5); p = v093_u32(p, 4);
    arc_m4 id = arc_m4_identity(); for (int i = 0; i < 16; i++) p = v093_f(p, id.m[i]);
    for (int j = 0; j < 5; j++) {
        memset(p, 0, 32); strcpy((char *)p, names[j]); p += 32; p = v093_u32(p, (uint32_t)(j - 1));
        for (int i = 0; i < 16; i++) p = v093_f(p, id.m[i]);
        p = v093_trs(p, v093_rest[j][0], v093_rest[j][1], v093_rest[j][2]);
    }
    static const float idle[2][3] = { { 0, 2, 0 }, { 0, 2, 0 } };
    static const float walk[3][3] = { { 0, 2, 0 }, { 1, 2, 0 }, { 0, 2, 0 } };
    static const float run[3][3] = { { 0, 2, 0 }, { 2, 2, 0 }, { 0, 2, 0 } };
    static const float jump[2][3] = { { 0, 2, 0 }, { 0, 3, 0 } };
    p = v093_clip(p, "idle", 1, 2, true, idle);
    p = v093_clip(p, "walk", 2, 3, true, walk);
    p = v093_clip(p, "run", 4, 3, true, run);
    p = v093_clip(p, "jump", 2, 2, false, jump);
    v093_n = (size_t)(p - v093_buf);
    return arc_anim_from_memory(v093_buf, v093_n);
}
static float v093_hips_x(const arc_animator *an) { arc_pose ps; arc_animator_pose(an, &ps); return arc_pose_joint_pos(&ps, 0).x; }

static void v093_graph(void) {
    arc_anim rig = v093_rig();
    CHECK(rig.id != 0 && arc_anim_clip_count(rig) == 4);
    int c_idle = arc_anim_find(rig, "idle"), c_walk = arc_anim_find(rig, "walk"), c_run = arc_anim_find(rig, "run"), c_jump = arc_anim_find(rig, "jump");
    CHECK(fabsf(arc_anim_length(rig, c_walk) - 1.0f) < 1e-6f && fabsf(arc_anim_length(rig, c_run) - 0.5f) < 1e-6f);
    arc_anim_graph g = arc_anim_graph_create(rig);
    int speed = arc_anim_graph_param(g, "speed", ARC_ANIM_FLOAT), jump = arc_anim_graph_param(g, "jump", ARC_ANIM_TRIGGER);
    CHECK(speed == 0 && jump == 1 && arc_anim_graph_param(g, "speed", ARC_ANIM_FLOAT) == 0 && arc_anim_graph_find_param(g, "nope") == -1);
    int idle = arc_anim_graph_state(g, "idle", c_idle, 1, true);
    int clips[2] = { c_walk, c_run }; float pts[2] = { 1, 3 };
    int move = arc_anim_graph_blend(g, "move", speed, clips, pts, 2, true);
    int air = arc_anim_graph_state(g, "jump", c_jump, 1, false);
    CHECK(idle == 0 && move == 1 && air == 2 && arc_anim_graph_find_state(g, "move") == 1);
    arc_anim_graph_when(g, arc_anim_graph_transition(g, idle, move, 0.2f, -1), speed, ARC_ANIM_GT, 0.1f);
    arc_anim_graph_when(g, arc_anim_graph_transition(g, move, idle, 0.2f, -1), speed, ARC_ANIM_LE, 0.1f);
    arc_anim_graph_when(g, arc_anim_graph_transition(g, ARC_ANIM_ANY, air, 0.1f, -1), jump, ARC_ANIM_SET, 0);
    arc_anim_graph_transition(g, air, idle, 0.1f, 1.0f);
    int step = arc_anim_graph_event(g, c_walk, 0.25f, "step");
    CHECK(arc_anim_graph_event(g, c_walk, 0.75f, "step") == step);
    arc_anim_graph_event(g, c_run, 0.125f, "step"); arc_anim_graph_event(g, c_run, 0.375f, "step");
    int land = arc_anim_graph_event(g, c_jump, 0.4f, "land");
    CHECK(land != step && !strcmp(arc_anim_graph_event_name(g, land), "land"));

    arc_animator an; arc_animator_init(&an, g, idle);
    CHECK(!strcmp(arc_animator_state_name(&an), "idle") && an.prev == -1);
    arc_animator_update(&an, 1.0f / 60);
    CHECK(an.state == idle);                                         /* speed 0: stays */
    /* speed up: idle -> move with a 0.2 s crossfade */
    arc_animator_set(&an, speed, 1);
    arc_animator_update(&an, 1.0f / 60);
    CHECK(an.state == move && an.prev == idle && fabsf(an.fade_len - 0.2f) < 1e-6f && an.changes == 1);
    for (int i = 0; i < 12; i++) arc_animator_update(&an, 1.0f / 60);
    CHECK(an.prev == -1);                                            /* the fade is over */
    /* walking (speed 1 = the walk clip only, 1 s a loop): two footsteps a second */
    int steps = 0; for (int i = 0; i < 60; i++) { arc_animator_update(&an, 1.0f / 60); steps += arc_animator_fired(&an, step); }
    CHECK(steps == 2);
    /* speed 2: half walk, half run; the blend's loop is 0.75 s; at phase 0.2 the hips are at 0.5 x 0.4 + 0.5 x 0.8 */
    arc_animator_set(&an, speed, 2); an.time = 0.2f;
    CHECK(fabsf(v093_hips_x(&an) - 0.6f) < 1e-4f);
    steps = 0; for (int i = 0; i < 90; i++) { arc_animator_update(&an, 1.0f / 60); steps += arc_animator_fired(&an, step); }
    CHECK(steps == 4);                                               /* 1.5 s = two loops of 0.75 s, two steps each */
    /* a trigger: any state -> jump, the trigger is used up; the jump ends (exit time 1) and goes back to idle */
    arc_animator_trigger(&an, jump);                                   /* (still at speed 2: move -> idle doesn't pass first) */
    arc_animator_update(&an, 1.0f / 60);
    CHECK(an.state == air && an.param[jump] == 0);
    arc_animator_set(&an, speed, 0);
    int lands = 0, back = -1;
    for (int i = 0; i < 40; i++) { arc_animator_update(&an, 1.0f / 60); lands += arc_animator_fired(&an, land); if (back < 0 && an.state == idle) back = i; }
    CHECK(lands == 1);
    CHECK(back >= 28 && back <= 31);                                 /* 0.5 s into the jump */
    CHECK(an.state == idle);
    /* play: a forced crossfade; halfway through it the pose is halfway between the two */
    arc_animator_set(&an, speed, 1);
    arc_animator an2 = an;
    arc_animator_play(&an, move, 0.2f);
    for (int i = 0; i < 6; i++) arc_animator_update(&an, 1.0f / 60);
    CHECK(an.state == move && an.prev == idle);
    CHECK(fabsf(v093_hips_x(&an) - 0.1f) < 1e-3f);                    /* walk at 0.1 s is x 0.2, idle 0, half each */
    /* plain data and deterministic: the same updates give the same bytes */
    arc_animator an3 = an2;
    for (int i = 0; i < 100; i++) { arc_animator_update(&an2, 1.0f / 60); arc_animator_update(&an3, 1.0f / 60); }
    CHECK(memcmp(&an2, &an3, sizeof an2) == 0);
    CHECK(arc_animator_setf(&an2, "speed", 0.5f) && !arc_animator_setf(&an2, "nope", 1));
    CHECK(arc_animator_progress(&an2) >= 0 && arc_animator_progress(&an2) < 1);
    arc_anim_graph_destroy(g);
    arc_anim_destroy(rig);
}
static float v093_dist(arc_v3 a, arc_v3 b) { return arc_v3_len(arc_v3_sub(a, b)); }
static void v093_ik(void) {
    arc_anim rig = v093_rig();
    arc_pose rest; arc_anim_pose(rig, NULL, 0, &rest);
    CHECK(rest.count == 5 && fabsf(arc_pose_joint_pos(&rest, 3).y) < 1e-6f);   /* the ankle at the floor */
    int ankle = arc_anim_joint(rig, "ankle"), knee = 2, thigh = 1, toe = 4;
    arc_pose p = rest;
    arc_v3 target = arc_v3_make(0.5f, 0.3f, 0.6f), pole = arc_v3_make(0, 1, 5);
    CHECK(arc_pose_two_bone_ik(rig, &p, ankle, target, pole, 1, false));
    CHECK(v093_dist(arc_pose_joint_pos(&p, ankle), target) < 1e-3f);
    CHECK(fabsf(v093_dist(arc_pose_joint_pos(&p, knee), arc_pose_joint_pos(&p, thigh)) - 1) < 1e-4f);
    CHECK(fabsf(v093_dist(arc_pose_joint_pos(&p, ankle), arc_pose_joint_pos(&p, knee)) - 1) < 1e-4f);
    CHECK(fabsf(v093_dist(arc_pose_joint_pos(&p, toe), arc_pose_joint_pos(&p, ankle)) - 0.3f) < 1e-4f);   /* the toe followed */
    /* the knee bends toward the pole: in front of the line from the thigh to the target */
    arc_v3 a = arc_pose_joint_pos(&p, thigh), n = arc_v3_norm(arc_v3_sub(target, a)), k = arc_v3_sub(arc_pose_joint_pos(&p, knee), a);
    arc_v3 side = arc_v3_sub(k, arc_v3_scale(n, arc_v3_dot(k, n)));
    CHECK(side.z > 0.1f);
    /* keep the tip's rotation: the toe stays 0.3 straight forward of the ankle */
    p = rest;
    arc_pose_two_bone_ik(rig, &p, ankle, target, pole, 1, true);
    arc_v3 ft = arc_v3_sub(arc_pose_joint_pos(&p, toe), arc_pose_joint_pos(&p, ankle));
    CHECK(fabsf(ft.x) < 1e-4f && fabsf(ft.y) < 1e-4f && fabsf(ft.z - 0.3f) < 1e-4f);
    CHECK(v093_dist(arc_pose_joint_pos(&p, ankle), target) < 1e-3f);
    /* a pole on the other side bends the knee the other way */
    p = rest; arc_pose_two_bone_ik(rig, &p, ankle, target, arc_v3_make(0, 1, -5), 1, false);
    k = arc_v3_sub(arc_pose_joint_pos(&p, knee), a); side = arc_v3_sub(k, arc_v3_scale(n, arc_v3_dot(k, n)));
    CHECK(side.z < -0.1f);
    /* out of reach: false, the leg points straight at the target, fully stretched */
    p = rest;
    CHECK(!arc_pose_two_bone_ik(rig, &p, ankle, arc_v3_make(3, 2, 0), pole, 1, false));
    CHECK(v093_dist(arc_pose_joint_pos(&p, ankle), arc_v3_make(2, 2, 0)) < 2e-2f);
    /* weight 0: unchanged; weight 0.5: part of the way */
    p = rest; arc_pose_two_bone_ik(rig, &p, ankle, target, pole, 0, false);
    CHECK(memcmp(&p, &rest, sizeof p) == 0);
    p = rest; arc_pose_two_bone_ik(rig, &p, ankle, target, pole, 0.5f, false);
    float d0 = v093_dist(arc_pose_joint_pos(&rest, ankle), target), d1 = v093_dist(arc_pose_joint_pos(&p, ankle), target);
    CHECK(d1 < d0 * 0.8f && d1 > 1e-2f);
    /* a joint without a grandparent can't be a tip */
    p = rest; CHECK(!arc_pose_two_bone_ik(rig, &p, thigh, target, pole, 1, false));
    CHECK(memcmp(&p, &rest, sizeof p) == 0);
    /* IK on a pose from the animator (a walking pose) keeps working */
    arc_anim_layer L = { arc_anim_find(rig, "walk"), 0.3f, 1, true };
    arc_anim_pose(rig, &L, 1, &p);
    arc_v3 hips = arc_pose_joint_pos(&p, 0);
    arc_v3 t2 = arc_v3_add(hips, arc_v3_make(0.2f, -1.6f, 0.4f));
    CHECK(arc_pose_two_bone_ik(rig, &p, ankle, t2, arc_v3_add(hips, arc_v3_make(0, -1, 3)), 1, true));
    CHECK(v093_dist(arc_pose_joint_pos(&p, ankle), t2) < 1e-3f);
    arc_anim_destroy(rig);
}
static void v093_rot(arc_pose *p, int from, arc_quat q, arc_v3 pivot) {   /* the rig is a chain: joints from..4 turn together */
    arc_m4 G = arc_m4_mul(arc_m4_translate(pivot), arc_m4_mul(arc_m4_from_quat(q), arc_m4_translate(arc_v3_scale(pivot, -1))));
    for (int j = from; j < p->count; j++) p->joint[j] = arc_m4_mul(G, p->joint[j]);
}
static void v093_ik_fuzz(void) {   /* random reachable targets and poles from random bent starting poses */
    arc_anim rig = v093_rig();
    arc_rng r; arc_rng_seed(&r, 93, 1);
    int reached = 0, lengths = 0, pole_side = 0, n = 500;
    for (int i = 0; i < n; i++) {
        arc_pose p; arc_anim_pose(rig, NULL, 0, &p);
        /* bend the starting pose: turn the thigh and the knee by random amounts */
        arc_v3 ax = arc_v3_norm(arc_v3_make(arc_rng_float(&r) - 0.5f, arc_rng_float(&r) - 0.5f, arc_rng_float(&r) - 0.5f));
        v093_rot(&p, 1, arc_quat_axis_angle(ax, arc_rng_float(&r) * 2), arc_pose_joint_pos(&p, 1));
        v093_rot(&p, 2, arc_quat_axis_angle(ax, arc_rng_float(&r) * 2), arc_pose_joint_pos(&p, 2));
        arc_v3 a = arc_pose_joint_pos(&p, 1);
        arc_v3 dir = arc_v3_norm(arc_v3_make(arc_rng_float(&r) - 0.5f, arc_rng_float(&r) - 0.5f, arc_rng_float(&r) - 0.5f));
        float d = 0.2f + arc_rng_float(&r) * 1.75f;   /* 0.2 .. 1.95 of a 2.0 reach */
        arc_v3 t = arc_v3_add(a, arc_v3_scale(dir, d));
        arc_v3 pole = arc_v3_add(a, arc_v3_scale(arc_v3_norm(arc_v3_make(arc_rng_float(&r) - 0.5f, arc_rng_float(&r) - 0.5f, arc_rng_float(&r) - 0.5f)), 3));
        arc_pose_two_bone_ik(rig, &p, 3, t, pole, 1, (i & 1) != 0);
        reached += v093_dist(arc_pose_joint_pos(&p, 3), t) < 2e-3f;
        lengths += fabsf(v093_dist(arc_pose_joint_pos(&p, 2), a) - 1) < 1e-3f && fabsf(v093_dist(arc_pose_joint_pos(&p, 3), arc_pose_joint_pos(&p, 2)) - 1) < 1e-3f;
        /* the knee lies on the pole's side of the hip-target line (when the pole isn't on that line) */
        arc_v3 nn = arc_v3_norm(arc_v3_sub(t, a)), kb = arc_v3_sub(arc_pose_joint_pos(&p, 2), a), pv = arc_v3_sub(pole, a);
        kb = arc_v3_sub(kb, arc_v3_scale(nn, arc_v3_dot(kb, nn))); pv = arc_v3_sub(pv, arc_v3_scale(nn, arc_v3_dot(pv, nn)));
        pole_side += arc_v3_len(pv) < 0.3f || arc_v3_len(kb) < 1e-3f || arc_v3_dot(kb, pv) > 0;
    }
    if (reached != n || lengths != n || pole_side != n) printf("ARC note v093 ik fuzz: reached %d, lengths %d, pole side %d of %d\n", reached, lengths, pole_side, n);
    CHECK(reached == n); CHECK(lengths == n); CHECK(pole_side == n);
    arc_anim_destroy(rig);
}
static void v093_init(void) { v093_graph(); v093_ik(); v093_ik_fuzz(); }
