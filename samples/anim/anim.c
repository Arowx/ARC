/* ARC sample (0.9.3): an animation graph and foot IK on a stick figure built in code (no art files).
   W/S speed up and slow down (idle -> walk -> run blend by speed), SPACE jumps (a trigger; the jump returns to the
   blend when its clip ends), I turns foot IK on and off. The ground has bumps: with IK on, each foot is placed on the
   ground under it (two-bone IK, knee toward the front, the foot kept flat) and the hips drop to the lower foot.
   Footstep events flash the foot that lands. Agent commands: `speed V`, `jump`, `ik on|off`. */
#include "../../include/arc.h"
#include <string.h>
#include <stdlib.h>

enum { HIPS, SPINE, HEAD, LTHIGH, LKNEE, LANKLE, LTOE, RTHIGH, RKNEE, RANKLE, RTOE, NJ };
static const char *NAMES[NJ] = { "hips", "spine", "head", "l_thigh", "l_knee", "l_ankle", "l_toe", "r_thigh", "r_knee", "r_ankle", "r_toe" };
static const int PARENT[NJ] = { -1, HIPS, SPINE, HIPS, LTHIGH, LKNEE, LANKLE, HIPS, RTHIGH, RKNEE, RANKLE };
static const float REST[NJ][3] = { { 0, 1.0f, 0 }, { 0, 0.3f, 0 }, { 0, 0.45f, 0 }, { 0.15f, 0, 0 }, { 0, -0.45f, 0 }, { 0, -0.45f, 0 }, { 0, -0.1f, 0.15f },
                                   { -0.15f, 0, 0 }, { 0, -0.45f, 0 }, { 0, -0.45f, 0 }, { 0, -0.1f, 0.15f } };

typedef struct { arc_animator an; float x, speed_target; bool ik; int flash[2]; int steps; } Game;
static Game G;
static arc_anim rig; static arc_anim_graph graph;
static int P_SPEED, P_JUMP, EV_LSTEP, EV_RSTEP;
static arc_mesh m_joint, m_ground;

/* ---- the rig and its clips, written as an .arca in memory ---- */
static uint8_t buf[1 << 16]; static uint8_t *bp;
static void w32(uint32_t v) { memcpy(bp, &v, 4); bp += 4; }
static void wf(float v) { memcpy(bp, &v, 4); bp += 4; }
static void wtrs(const float t[3], float rx) {   /* translation and a turn about x */
    wf(t[0]); wf(t[1]); wf(t[2]); wf(sinf(rx * 0.5f)); wf(0); wf(0); wf(cosf(rx * 0.5f)); wf(1); wf(1); wf(1);
}
/* a clip: frames x joints; pose(f, j, &t, &rx) fills each joint */
static void clip(const char *name, float fps, int frames, bool loop, void (*pose)(float ph, int j, float t[3], float *rx)) {
    memset(bp, 0, 32); strcpy((char *)bp, name); bp += 32; wf(fps); w32((uint32_t)frames); w32(loop ? 1 : 0);
    for (int f = 0; f < frames; f++) for (int j = 0; j < NJ; j++) {
        float t[3] = { REST[j][0], REST[j][1], REST[j][2] }, rx = 0;
        pose(loop ? (float)f / ((float)(frames - 1)) : (float)f / ((float)(frames - 1)), j, t, &rx);
        wtrs(t, rx);
    }
}
static void p_idle(float ph, int j, float t[3], float *rx) { if (j == HIPS) t[1] = 1.0f - 0.01f * sinf(ph * 6.2832f); if (j == SPINE) *rx = 0.03f * sinf(ph * 6.2832f); }
static void gait(float ph, int j, float t[3], float *rx, float swing, float bob) {
    float s = sinf(ph * 6.2832f), c = cosf(ph * 6.2832f);
    if (j == HIPS) t[1] = 1.0f - bob * (1 - fabsf(c));
    if (j == SPINE) *rx = 0.1f * swing;
    if (j == LTHIGH) *rx = -swing * s;
    if (j == RTHIGH) *rx = swing * s;
    if (j == LKNEE) *rx = swing * 1.2f * fmaxf(0, -c) + 0.1f;   /* bend on the forward swing */
    if (j == RKNEE) *rx = swing * 1.2f * fmaxf(0, c) + 0.1f;
}
static void p_walk(float ph, int j, float t[3], float *rx) { gait(ph, j, t, rx, 0.45f, 0.03f); }
static void p_run(float ph, int j, float t[3], float *rx) { gait(ph, j, t, rx, 0.9f, 0.08f); }
static void p_jump(float ph, int j, float t[3], float *rx) {
    float h = sinf(ph * 3.1416f);
    if (j == HIPS) t[1] = 1.0f + 0.6f * h;
    if (j == LKNEE || j == RKNEE) *rx = 0.9f * h;
    if (j == LTHIGH || j == RTHIGH) *rx = -0.6f * h;
}
static arc_anim make_rig(void) {
    bp = buf; memcpy(bp, "ARCA", 4); bp += 4; w32(1); w32(NJ); w32(4);
    arc_m4 id = arc_m4_identity(); for (int i = 0; i < 16; i++) wf(id.m[i]);
    for (int j = 0; j < NJ; j++) {
        memset(bp, 0, 32); strcpy((char *)bp, NAMES[j]); bp += 32; w32((uint32_t)PARENT[j]);
        for (int i = 0; i < 16; i++) wf(id.m[i]);
        wtrs(REST[j], 0);
    }
    clip("idle", 8, 17, true, p_idle);
    clip("walk", 16, 17, true, p_walk);     /* 1 s a loop */
    clip("run", 24, 15, true, p_run);       /* 0.58 s */
    clip("jump", 24, 17, false, p_jump);    /* 0.67 s */
    return arc_anim_from_memory(buf, (size_t)(bp - buf));
}

static float ground(float x, float z) { return 0.12f * sinf(x * 1.3f) + 0.08f * sinf(x * 2.9f + z * 2.0f); }

static void init(void) {
    rig = make_rig();
    graph = arc_anim_graph_create(rig);
    P_SPEED = arc_anim_graph_param(graph, "speed", ARC_ANIM_FLOAT); P_JUMP = arc_anim_graph_param(graph, "jump", ARC_ANIM_TRIGGER);
    int clips[3] = { arc_anim_find(rig, "idle"), arc_anim_find(rig, "walk"), arc_anim_find(rig, "run") }; float pts[3] = { 0, 1.4f, 4.0f };
    int move = arc_anim_graph_blend(graph, "move", P_SPEED, clips, pts, 3, true);
    int air = arc_anim_graph_state(graph, "jump", arc_anim_find(rig, "jump"), 1, false);
    arc_anim_graph_when(graph, arc_anim_graph_transition(graph, move, air, 0.08f, -1), P_JUMP, ARC_ANIM_SET, 0);
    arc_anim_graph_transition(graph, air, move, 0.15f, 1.0f);
    /* footsteps: the left foot lands at a quarter of the gait, the right at three quarters */
    for (int c = 1; c <= 2; c++) {
        float len = arc_anim_length(rig, clips[c]);
        EV_LSTEP = arc_anim_graph_event(graph, clips[c], len * 0.25f, "left_step");
        EV_RSTEP = arc_anim_graph_event(graph, clips[c], len * 0.75f, "right_step");
    }
    arc_animator_init(&G.an, graph, move);
    G.ik = true;
    m_joint = arc_mesh_sphere(0xF0E0C0, 10); arc_mesh_set_name(m_joint, "joint");
    m_ground = arc_mesh_cube(0x5A7A4A); arc_mesh_set_name(m_ground, "ground");
}
static void tick(void) {
    if (arc_key_down(ARC_KEY_W)) G.speed_target = fminf(G.speed_target + 3 * arc_dt(), 4);
    if (arc_key_down(ARC_KEY_S)) G.speed_target = fmaxf(G.speed_target - 3 * arc_dt(), 0);
    if (arc_key_pressed(ARC_KEY_SPACE)) arc_animator_trigger(&G.an, P_JUMP);
    if (arc_key_pressed(ARC_KEY_I)) G.ik = !G.ik;
    float sp = G.an.param[P_SPEED]; sp += arc_clampf(G.speed_target - sp, -4 * arc_dt(), 4 * arc_dt());
    arc_animator_set(&G.an, P_SPEED, sp);
    arc_animator_update(&G.an, arc_dt());
    G.x += sp * arc_dt();
    if (G.x > 12) G.x -= 24;
    for (int f = 0; f < 2; f++) if (G.flash[f] > 0) G.flash[f]--;
    if (arc_animator_fired(&G.an, EV_LSTEP)) { G.flash[0] = 8; G.steps++; }
    if (arc_animator_fired(&G.an, EV_RSTEP)) { G.flash[1] = 8; G.steps++; }
}
/* the pose for drawing: animation, then (with IK) feet on the ground and the hips lowered to the lower foot */
static void final_pose(arc_pose *p, arc_m4 *model) {
    arc_animator_pose(&G.an, p);
    float drop = 0;
    if (G.ik && strcmp(arc_animator_state_name(&G.an), "jump") != 0) {
        const int ank[2] = { LANKLE, RANKLE };
        for (int f = 0; f < 2; f++) {
            arc_v3 a = arc_pose_joint_pos(p, ank[f]);
            float want = ground(G.x + a.x, a.z) + 0.1f;   /* the ankle sits 0.1 above the ground */
            float d = want - a.y; if (d < drop) drop = d;
        }
        drop = fmaxf(drop, -0.3f);
        *model = arc_m4_translate(arc_v3_make(G.x, drop, 0));
        /* in model space: the hips moved down by `drop`, so ground heights are relative to it */
        for (int f = 0; f < 2; f++) {
            arc_v3 a = arc_pose_joint_pos(p, ank[f]);
            float want = ground(G.x + a.x, a.z) + 0.1f - drop;
            if (a.y < want + 0.05f) {   /* only feet near the ground are planted; a swinging foot stays free */
                arc_v3 hip = arc_pose_joint_pos(p, f ? RTHIGH : LTHIGH);
                arc_pose_two_bone_ik(rig, p, ank[f], arc_v3_make(a.x, fmaxf(a.y, want), a.z), arc_v3_add(hip, arc_v3_make(0, -0.4f, 1)), 1, true);
            }
        }
    } else *model = arc_m4_translate(arc_v3_make(G.x, 0, 0));
}
static void draw(float alpha) {
    (void)alpha;
    arc_camera cam = { { G.x - 1.1f, 1.2f, 2.5f }, { G.x, 0.7f, 0 }, { 0, 1, 0 }, 0.8f, 0, 0.1f, 100 };
    arc_set_camera(&cam);
    arc_set_light(arc_v3_make(0.3f, 1, 0.6f), 0xFFF2E0, 0x485060);
    arc_set_sky(0x5080C0, 0xB0C8E0);
    for (int i = -40; i < 40; i++) for (int k = -3; k <= 3; k++) {   /* the bumpy ground as small columns */
        float x = floorf(G.x) + (float)i * 0.25f + 0.125f, z = (float)k * 0.25f;
        float h = ground(x, z);
        arc_draw_mesh(m_ground, arc_m4_trs_y(arc_v3_make(x, (h - 1) * 0.5f, z), 0, arc_v3_make(0.25f, h + 1, 0.25f)), ((i + k) & 1) ? 0xFFFFFF : 0xE0E8E0);
    }
    arc_pose p; arc_m4 model; final_pose(&p, &model);
    for (int j = 0; j < NJ; j++) {
        arc_v3 a = arc_m4_mul_point(model, arc_pose_joint_pos(&p, j));
        bool lit = (j == LANKLE && G.flash[0]) || (j == RANKLE && G.flash[1]);
        arc_draw_mesh(m_joint, arc_m4_trs_y(a, 0, arc_v3_make(0.07f, 0.07f, 0.07f)), lit ? 0xFF4040 : 0xFFFFFF);
        if (PARENT[j] >= 0) arc_draw_line(arc_m4_mul_point(model, arc_pose_joint_pos(&p, PARENT[j])), a, j >= RTHIGH ? 0x60A0FF : 0xFFC060);
    }
    arc_draw_textf(10, 10, 2, 0xFFFFFF, "%s  speed %.1f  progress %.2f  IK %s  steps %d", arc_animator_state_name(&G.an), G.an.param[P_SPEED], arc_animator_progress(&G.an), G.ik ? "on" : "off", G.steps);
    arc_draw_text(10, 32, 2, 0xD0D8E8, "W/S speed   SPACE jump   I foot IK");
}
static void dump(arc_json *j) {
    arc_pose p; arc_m4 model; final_pose(&p, &model);
    arc_json_str_k(j, "state", arc_animator_state_name(&G.an)); arc_json_num_k(j, "speed", G.an.param[P_SPEED]);
    arc_json_num_k(j, "steps", G.steps); arc_json_bool_k(j, "ik", G.ik); arc_json_num_k(j, "x", G.x);
    for (int f = 0; f < 2; f++) {   /* each foot's height above the ground under it (about 0.1 with IK when planted) */
        arc_v3 a = arc_m4_mul_point(model, arc_pose_joint_pos(&p, f ? RANKLE : LANKLE));
        arc_json_num_k(j, f ? "right_foot_clearance" : "left_foot_clearance", a.y - ground(a.x, a.z));
    }
}
static bool command(const char *name, int argc, const char **argv) {
    if (!strcmp(name, "speed") && argc >= 1) { G.speed_target = (float)atof(argv[0]); return true; }
    if (!strcmp(name, "jump")) { arc_animator_trigger(&G.an, P_JUMP); return true; }
    if (!strcmp(name, "ik") && argc >= 1) { G.ik = strcmp(argv[0], "off") != 0; return true; }
    return false;
}
static size_t save_state(void *b, size_t cap) { if (cap >= sizeof G) memcpy(b, &G, sizeof G); return sizeof G; }
static bool load_state(const void *b, size_t size) { if (size != sizeof G) return false; memcpy(&G, b, sizeof G); return true; }
arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "ARC animation graph + IK", .width = 960, .height = 600, .clear_color = 0xB0C8E0, .init = init,
                      .tick = tick, .draw = draw, .dump = dump, .command = command, .save_state = save_state, .load_state = load_state };
}
