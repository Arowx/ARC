/* ARC 0.9.8 self-test: ball joint cones and twist limits, and ragdolls. Included by test_core.c; v098r_init() runs in
   init. Names start with v098r_ (one translation unit). */
#define V098R_DT (1.0f / 60)
static void v098r_run(arc_physics w, float seconds) { int n = (int)(seconds * 60 + 0.5f); for (int i = 0; i < n; i++) arc_physics_step(w, V098R_DT); }

/* a capsule hanging from a world pivot on a ball joint, its axis down, kicked sideways (cone) or spun (twist) */
static float v098r_hang(float cone, bool twist_limit, arc_v3 vel, arc_v3 spin, float *max_twist) {
    arc_physics w = arc_physics_create(&(arc_physics_desc){ .no_sleep = true });
    int b = arc_physics_add(w, &(arc_body_desc){ .shape = ARC_SHAPE_CAPSULE, .size = { 0.05f, 0.4f }, .pos = { 0, -0.45f, 0 }, .vel = vel, .ang_vel = spin, .mass = 2 });
    int j = arc_physics_add_joint(w, &(arc_joint_desc){ .type = ARC_JOINT_BALL, .a = b, .b = 0, .anchor = { 0, 0, 0 }, .axis = { 0, -1, 0 },
                                                         .cone = cone, .cone_axis = { 0, -1, 0 }, .limit = twist_limit, .lower = -0.3f, .upper = 0.3f });
    float sw = 0, tw = 0;
    for (int i = 0; i < 180; i++) { arc_physics_step(w, V098R_DT); sw = fmaxf(sw, arc_physics_joint_swing(w, j)); tw = fmaxf(tw, fabsf(arc_physics_joint_angle(w, j))); }
    if (max_twist) *max_twist = tw;
    arc_physics_destroy(w);
    return sw;
}
static void v098r_cone_twist(void) {
    float t0, t1, t2;
    float free_swing = v098r_hang(0, false, arc_v3_make(6, 0, 0), arc_v3_make(0, 0, 0), NULL);
    float coned = v098r_hang(0.4f, false, arc_v3_make(6, 0, 0), arc_v3_make(0, 0, 0), NULL);
    float diag = v098r_hang(0.4f, false, arc_v3_make(4, 0, 4), arc_v3_make(0, 0, 0), NULL);
    v098r_hang(0, false, arc_v3_make(0, 0, 0), arc_v3_make(0, 8, 0), &t0);
    v098r_hang(0, true, arc_v3_make(0, 0, 0), arc_v3_make(0, 8, 0), &t1);
    float sw2 = v098r_hang(0.4f, true, arc_v3_make(3, 0, 0), arc_v3_make(0, 8, 0), &t2);
    printf("ARC note cone: free swing %.3f, cone 0.4 -> %.3f (diagonal %.3f); twist free %.3f, limited +-0.3 -> %.3f; both %.3f / %.3f\n", free_swing, coned, diag, t0, t1, sw2, t2);
    CHECK(free_swing > 1.0f);
    CHECK(coned < 0.45f && coned > 0.35f);
    CHECK(diag < 0.45f);
    CHECK(t0 > 1.0f);
    CHECK(t1 < 0.34f && t1 > 0.25f);
    CHECK(sw2 < 0.46f && t2 < 0.36f);
}

static float v098r_floor_check(arc_physics w, const arc_ragdoll *r) {   /* the lowest point of any part (should not be below 0) */
    float lo = 1e9f;
    for (int p = 0; p < ARC_RAG_PARTS; p++) {
        arc_m4 m = arc_physics_matrix(w, r->body[p]); arc_v3 c = arc_v3_make(m.m[12], m.m[13], m.m[14]), y = arc_v3_make(m.m[4], m.m[5], m.m[6]);
        float h = fabsf(y.y) * r->half[p];
        lo = fminf(lo, c.y - h - r->radius[p]);
    }
    return lo;
}
static void v098r_humanoid(void) {
    arc_physics w = arc_physics_create(NULL); arc_physics_add_plane(w, arc_v3_make(0, 1, 0), 0);
    arc_ragdoll r;
    CHECK(arc_ragdoll_humanoid(w, arc_v3_make(0, 0.02f, 0), 0.3f, 1.8f, &(arc_ragdoll_desc){ .mass = 80, .vel = { 0, 0, -1.5f } }, &r));
    CHECK(arc_physics_body_count(w) == ARC_RAG_PARTS && arc_physics_joint_count(w) == ARC_RAG_PARTS - 1);
    float h0 = arc_physics_pos(w, r.body[ARC_RAG_HEAD]).y;
    CHECK(fabsf(h0 - 1.68f) < 0.08f);
    /* it falls over backwards; the limits hold the whole way down */
    float worst_knee = 0, worst_elbow = 0, worst_hip = 0, worst_lo = 0; int frames = 0, asleep_at = -1;
    for (int i = 0; i < 60 * 10; i++) {
        arc_physics_step(w, V098R_DT); frames++;
        for (int s = 0; s < 2; s++) {
            int knee = r.joint[s ? ARC_RAG_SHIN_R : ARC_RAG_SHIN_L], elbow = r.joint[s ? ARC_RAG_FOREARM_R : ARC_RAG_FOREARM_L], hip = r.joint[s ? ARC_RAG_THIGH_R : ARC_RAG_THIGH_L];
            float ka = arc_physics_joint_angle(w, knee), ea = arc_physics_joint_angle(w, elbow);
            worst_knee = fmaxf(worst_knee, fmaxf(-0.02f - ka, ka - 2.6f));   /* humanoid knees start straight: 0 .. 2.6 (so >0 is past a limit) */
            worst_elbow = fmaxf(worst_elbow, fmaxf(-0.05f - ea, ea - 2.6f));
            worst_hip = fmaxf(worst_hip, arc_physics_joint_swing(w, hip) - 1.1f);
        }
        worst_lo = fminf(worst_lo, v098r_floor_check(w, &r));
        bool all = true; for (int p = 0; p < ARC_RAG_PARTS; p++) all = all && arc_physics_sleeping(w, r.body[p]);
        if (all && asleep_at < 0) asleep_at = i;
    }
    float head = arc_physics_pos(w, r.body[ARC_RAG_HEAD]).y, pelvis = arc_physics_pos(w, r.body[ARC_RAG_PELVIS]).y;
    printf("ARC note ragdoll: head %.3f -> %.3f, pelvis %.3f, past limits knee %.3f elbow %.3f hip %.3f rad, lowest point %.3f m, asleep after %.2f s\n",
           h0, head, pelvis, worst_knee, worst_elbow, worst_hip, worst_lo, (float)asleep_at / 60.0f);
    CHECK(head < 0.3f && pelvis < 0.3f);       /* lying down */
    CHECK(worst_knee < 0.12f && worst_elbow < 0.12f && worst_hip < 0.12f);
    CHECK(worst_lo > -0.03f);
    CHECK(asleep_at > 0);
    arc_ragdoll_remove(w, &r);
    CHECK(arc_physics_body_count(w) == 0 && arc_physics_joint_count(w) == 0);
    /* a full world is refused */
    arc_physics small = arc_physics_create(&(arc_physics_desc){ .max_bodies = 8 });
    CHECK(!arc_ragdoll_humanoid(small, arc_v3_make(0, 0, 0), 0, 1.8f, NULL, &r));
    arc_physics_destroy(small);
    arc_physics_destroy(w);
}

/* a mixamo-style skeleton in centimetres under a 0.01 root scale (as a converted mixamo GLB), facing +z */
static uint8_t v098r_buf[8192]; static size_t v098r_n;
enum { V098R_HIPS, V098R_SPINE, V098R_SPINE2, V098R_NECK, V098R_HEAD, V098R_TOP, V098R_LSH, V098R_LARM, V098R_LFORE, V098R_LHAND, V098R_RSH, V098R_RARM, V098R_RFORE, V098R_RHAND,
       V098R_LUP, V098R_LLEG, V098R_LFOOT, V098R_LTOE, V098R_RUP, V098R_RLEG, V098R_RFOOT, V098R_RTOE, V098R_NJ };
static const char *v098r_names[V098R_NJ] = { "mixamorig:Hips", "mixamorig:Spine", "mixamorig:Spine2", "mixamorig:Neck", "mixamorig:Head", "mixamorig:HeadTop_End",
    "mixamorig:LeftShoulder", "mixamorig:LeftArm", "mixamorig:LeftForeArm", "mixamorig:LeftHand", "mixamorig:RightShoulder", "mixamorig:RightArm", "mixamorig:RightForeArm", "mixamorig:RightHand",
    "mixamorig:LeftUpLeg", "mixamorig:LeftLeg", "mixamorig:LeftFoot", "mixamorig:LeftToeBase", "mixamorig:RightUpLeg", "mixamorig:RightLeg", "mixamorig:RightFoot", "mixamorig:RightToeBase" };
static const int v098r_par[V098R_NJ] = { -1, V098R_HIPS, V098R_SPINE, V098R_SPINE2, V098R_NECK, V098R_HEAD, V098R_SPINE2, V098R_LSH, V098R_LARM, V098R_LFORE, V098R_SPINE2, V098R_RSH, V098R_RARM, V098R_RFORE,
    V098R_HIPS, V098R_LUP, V098R_LLEG, V098R_LFOOT, V098R_HIPS, V098R_RUP, V098R_RLEG, V098R_RFOOT };
/* local offsets (cm) from the parent; the character's left is +x; arms hang slightly out, the left knee bent a little */
static const float v098r_off[V098R_NJ][3] = { { 0, 95, 0 }, { 0, 10, 0 }, { 0, 25, 0 }, { 0, 15, 0 }, { 0, 10, 2 }, { 0, 18, 0 },
    { 5, 8, 0 }, { 12, 0, 0 }, { 5, -28, 0 }, { 2, -26, 3 }, { -5, 8, 0 }, { -12, 0, 0 }, { -5, -28, 0 }, { -2, -26, 3 },
    { 10, -5, 0 }, { 0, -43, 4 }, { 0, -42, -6 }, { 0, -6, 12 }, { -10, -5, 0 }, { 0, -45, 0 }, { 0, -42, 0 }, { 0, -6, 12 } };
static uint8_t *v098r_w32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); return p + 4; }
static uint8_t *v098r_wf(uint8_t *p, float v) { memcpy(p, &v, 4); return p + 4; }
static arc_anim v098r_rig(int joints) {
    uint8_t *p = v098r_buf; memcpy(p, "ARCA", 4); p += 4; p = v098r_w32(p, 1); p = v098r_w32(p, (uint32_t)joints); p = v098r_w32(p, 0);
    arc_m4 root = arc_m4_scale(arc_v3_make(0.01f, 0.01f, 0.01f)); for (int i = 0; i < 16; i++) p = v098r_wf(p, root.m[i]);
    arc_m4 id = arc_m4_identity();
    for (int j = 0; j < joints; j++) {
        memset(p, 0, 32); strcpy((char *)p, v098r_names[j]); p += 32; p = v098r_w32(p, (uint32_t)v098r_par[j]);
        for (int i = 0; i < 16; i++) p = v098r_wf(p, id.m[i]);
        float trs[10] = { v098r_off[j][0], v098r_off[j][1], v098r_off[j][2], 0, 0, 0, 1, 1, 1, 1 }; for (int i = 0; i < 10; i++) p = v098r_wf(p, trs[i]);
    }
    v098r_n = (size_t)(p - v098r_buf);
    return arc_anim_from_memory(v098r_buf, v098r_n);
}
static float v098r_dist(const arc_pose *p, int a, int b) { return arc_v3_len(arc_v3_sub(arc_pose_joint_pos(p, a), arc_pose_joint_pos(p, b))); }
static void v098r_from_pose(void) {
    arc_anim rig = v098r_rig(V098R_NJ);
    arc_pose rest; arc_anim_pose(rig, NULL, 0, &rest);
    CHECK(fabsf(arc_pose_joint_pos(&rest, V098R_HIPS).y - 0.95f) < 1e-4f);
    arc_physics w = arc_physics_create(NULL); arc_physics_add_plane(w, arc_v3_make(0, 1, 0), 0);
    arc_m4 model = arc_m4_trs_y(arc_v3_make(2, 0.01f, -1), 0.7f, arc_v3_make(1, 1, 1));
    arc_ragdoll r;
    CHECK(arc_ragdoll_from_pose(w, rig, &rest, model, &(arc_ragdoll_desc){ .mass = 75 }, &r));
    CHECK(r.joints == V098R_NJ);
    CHECK(r.owner[V098R_LHAND] == ARC_RAG_FOREARM_L && r.owner[V098R_LTOE] == ARC_RAG_SHIN_L && r.owner[V098R_LSH] == ARC_RAG_CHEST && r.owner[V098R_TOP] == ARC_RAG_HEAD);
    /* straight away the ragdoll's pose is the pose it came from */
    arc_pose p; arc_ragdoll_pose(w, &r, model, &p);
    float err = 0; for (int j = 0; j < V098R_NJ; j++) err = fmaxf(err, arc_v3_len(arc_v3_sub(arc_pose_joint_pos(&p, j), arc_pose_joint_pos(&rest, j))));
    CHECK(err < 1e-3f);
    /* the left knee was bent: its hinge allows straightening from there, and bends further */
    int lk = r.joint[ARC_RAG_SHIN_L];
    CHECK(fabsf(arc_physics_joint_angle(w, lk)) < 1e-4f);
    /* after a fall the skeleton is still one piece: bones within a part keep their length exactly, bones across a
       joint stay within 2 cm, and the joints sit on their bodies */
    v098r_run(w, 4);
    arc_ragdoll_pose(w, &r, model, &p);
    float in_part = fabsf(v098r_dist(&p, V098R_LFORE, V098R_LHAND) - v098r_dist(&rest, V098R_LFORE, V098R_LHAND));
    float across = 0; const int pairs[][2] = { { V098R_LARM, V098R_LFORE }, { V098R_LUP, V098R_LLEG }, { V098R_RUP, V098R_RLEG }, { V098R_NECK, V098R_HEAD }, { V098R_HIPS, V098R_SPINE } };
    for (int i = 0; i < 5; i++) across = fmaxf(across, fabsf(v098r_dist(&p, pairs[i][0], pairs[i][1]) - v098r_dist(&rest, pairs[i][0], pairs[i][1])));
    arc_v3 hips_w = arc_m4_mul_point(model, arc_pose_joint_pos(&p, V098R_HIPS));
    float top = arc_m4_mul_point(model, arc_pose_joint_pos(&p, V098R_TOP)).y;
    printf("ARC note ragdoll from pose: bone in a part %.5f m, across joints %.4f m, hips at %.2f %.2f %.2f, head top %.2f\n", in_part, across, hips_w.x, hips_w.y, hips_w.z, top);
    CHECK(in_part < 1e-3f && across < 0.02f);
    CHECK(top < 0.4f && hips_w.y < 0.35f);
    arc_ragdoll_remove(w, &r);
    /* prev pose: each part starts with the pose's motion (here 2 m/s toward +x in the model, turned 0.7 rad) */
    arc_pose before = rest; for (int j = 0; j < V098R_NJ; j++) { before.joint[j].m[12] -= 0.1f; }
    CHECK(arc_ragdoll_from_pose(w, rig, &rest, model, &(arc_ragdoll_desc){ .prev = &before, .prev_dt = 0.05f }, &r));
    arc_v3 v = arc_physics_vel(w, r.body[ARC_RAG_CHEST]), want = arc_m4_mul_dir(model, arc_v3_make(2, 0, 0));
    CHECK(arc_v3_len(arc_v3_sub(v, want)) < 0.01f);
    arc_ragdoll_remove(w, &r);
    /* a skeleton without legs is refused, with a hint */
    arc_anim torso = v098r_rig(V098R_LUP);
    arc_pose tp; arc_anim_pose(torso, NULL, 0, &tp);
    CHECK(!arc_ragdoll_from_pose(w, torso, &tp, model, NULL, &r));
    CHECK(arc_physics_body_count(w) == 0);
    arc_anim_destroy(torso); arc_anim_destroy(rig);
    arc_physics_destroy(w);
}
static void v098r_same(void) {
    /* two ragdolls tumbling down a slope against each other: the same bytes every run, and across a save / load */
    uint8_t *a = NULL, *b = NULL, *mid = NULL; size_t na = 0, nb = 0, nm = 0;
    for (int pass = 0; pass < 3; pass++) {
        arc_physics w = arc_physics_create(NULL);
        arc_physics_add_plane(w, arc_v3_norm(arc_v3_make(0.3f, 1, 0)), 0);
        arc_ragdoll r1, r2;
        arc_ragdoll_humanoid(w, arc_v3_make(0, 1.5f, 0), 1.0f, 1.8f, &(arc_ragdoll_desc){ .vel = { 1, 0, 0 } }, &r1);
        arc_ragdoll_humanoid(w, arc_v3_make(1.0f, 1.0f, 0.2f), -0.5f, 1.6f, &(arc_ragdoll_desc){ .layer = 2, .mask = ~2u }, &r2);
        for (int i = 0; i < 240; i++) {
            arc_physics_step(w, V098R_DT);
            if (i == 119 && pass == 0) { nm = arc_physics_save(w, NULL, 0); mid = (uint8_t *)malloc(nm); arc_physics_save(w, mid, nm); }
            if (i == 119 && pass == 2) CHECK(arc_physics_load(w, mid, nm));
        }
        size_t n = arc_physics_save(w, NULL, 0); uint8_t *buf = (uint8_t *)malloc(n); arc_physics_save(w, buf, n);
        if (pass == 0) { a = buf; na = n; } else if (pass == 1) { b = buf; nb = n; } else { CHECK(n == na && memcmp(buf, a, n) == 0); free(buf); }
        arc_physics_destroy(w);
    }
    CHECK(na == nb && memcmp(a, b, na) == 0);
    free(a); free(b); free(mid);
}
static void v098r_init(void) { v098r_cone_twist(); v098r_humanoid(); v098r_from_pose(); v098r_same(); }
