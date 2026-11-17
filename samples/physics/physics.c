/* ARC sample (0.9.4): deterministic rigid bodies. A pyramid of crates, a pile of balls and capsules, a ramp made of
   triangles, and a spinning kinematic paddle. 0.9.7: joints: a rope of capsules on ball joints hangs from a post, and
   a gate on a hinge with limits swings back and forth on its motor (G reverses it). 0.9.8: F drops a ragdoll (up to 4)
   onto the pyramid. Click (or SPACE) shoots a ball where the mouse points; R resets; D shows
   the physics shapes; impacts make a sound scaled by their impulse. Agent commands: `shoot X Y Z` (from the camera
   toward a point), `ragdoll`, `reset`. dump() reports how many bodies are awake and where the pyramid's top crate is. */
#include "../../include/arc.h"
#include <string.h>
#include <stdlib.h>

#define MAXB 256
typedef struct { int body[MAXB]; uint8_t shape[MAXB]; arc_v3 size[MAXB]; uint32_t rgb[MAXB]; int n; int top, paddle; float spin; int shots; bool debug; int gate_joint; float gate_dir; int nrag; arc_ragdoll rag[4]; } Game;
static Game G;
static arc_physics phys;
static arc_mesh m_cube, m_sphere, m_cyl, m_ramp, m_floor;
static arc_sound s_hit;
static arc_camera cam;
static uint8_t state0[1 << 20]; static size_t state0_n; static Game g0;   /* the starting state, for reset */

static int add(arc_body_desc d, uint32_t rgb) {
    int b = arc_physics_add(phys, &d); if (!b || G.n >= MAXB) return b;
    G.body[G.n] = b; G.shape[G.n] = (uint8_t)d.shape; G.size[G.n] = d.size; G.rgb[G.n] = rgb; G.n++;
    return b;
}
static void shoot_toward(arc_v3 target) {
    arc_v3 dir = arc_v3_norm(arc_v3_sub(target, cam.eye));
    add((arc_body_desc){ .shape = ARC_SHAPE_SPHERE, .size = { 0.3f }, .pos = arc_v3_add(cam.eye, arc_v3_scale(dir, 1.5f)), .vel = arc_v3_scale(dir, 22), .mass = 20, .restitution = 0.3f }, 0xF04040);
    G.shots++;
}
static void drop_ragdoll(void) {   /* 0.9.8: a body falling from above the pyramid, tumbling a little */
    if (G.nrag >= 4) return;
    float k = (float)G.nrag;
    if (arc_ragdoll_humanoid(phys, arc_v3_make(1.2f + 0.6f * k, 6.5f + 0.5f * k, -0.3f + 0.2f * k), 0.8f * k, 1.7f,
                             &(arc_ragdoll_desc){ .mass = 70, .vel = { 0.5f, 0, 1.0f } }, &G.rag[G.nrag])) G.nrag++;
}
static void init(void) {
    phys = arc_physics_create(NULL);
    arc_physics_add_plane(phys, arc_v3_make(0, 1, 0), 0);
    m_cube = arc_mesh_cube(0xFFFFFF); m_sphere = arc_mesh_sphere(0xFFFFFF, 16); m_cyl = arc_mesh_cylinder(0xFFFFFF, 16);
    m_floor = arc_mesh_plane(0x50586A, 20, 0x485062); arc_mesh_set_name(m_floor, "floor");
    m_ramp = arc_mesh_plane(0x8A7050, 4, 0x7A6040); arc_mesh_set_name(m_ramp, "ramp");
    arc_mesh_set_name(m_cube, "crate"); arc_mesh_set_name(m_sphere, "ball"); arc_mesh_set_name(m_cyl, "capsule");
    /* the ramp: a plane mesh tilted 20 degrees, given to physics as triangles */
    arc_m4 ramp = arc_m4_mul(arc_m4_translate(arc_v3_make(-7, 1.4f, -2)), arc_m4_mul(arc_m4_rotate_z(-0.35f), arc_m4_scale(arc_v3_make(8, 1, 4))));
    arc_physics_add_mesh(phys, m_ramp, ramp);
    /* a pyramid of crates */
    int rows = 6;
    for (int r = 0; r < rows; r++) for (int c = 0; c < rows - r; c++)
        G.top = add((arc_body_desc){ .shape = ARC_SHAPE_BOX, .size = { 0.4f, 0.4f, 0.4f }, .pos = { 2 + ((float)c - ((float)(rows - r - 1)) * 0.5f) * 0.84f, 0.4f + (float)r * 0.8f, 0 }, .mass = 8 }, 0xC89050);
    /* balls and capsules that roll down the ramp */
    for (int i = 0; i < 6; i++) add((arc_body_desc){ .shape = ARC_SHAPE_SPHERE, .size = { 0.25f + 0.05f * (i % 3) }, .pos = { -9.5f + (float)i * 0.3f, 4 + (float)i * 0.7f, -2.5f + (i % 3) * 0.6f }, .restitution = 0.4f }, 0x60A0F0);
    for (int i = 0; i < 3; i++) add((arc_body_desc){ .shape = ARC_SHAPE_CAPSULE, .size = { 0.2f, 0.4f }, .pos = { -9, (float)(5 + i), -1.2f }, .rot = arc_quat_axis_angle(arc_v3_make(1, 0, 0), 1.5707963f) }, 0x70D070);
    /* a kinematic paddle sweeping the floor */
    G.paddle = add((arc_body_desc){ .type = ARC_BODY_KINEMATIC, .shape = ARC_SHAPE_BOX, .size = { 2.5f, 0.3f, 0.15f }, .pos = { 2, 0.3f, 5 } }, 0xE0E0E0);
    for (int i = 0; i < 8; i++) add((arc_body_desc){ .shape = ARC_SHAPE_BOX, .size = { 0.25f, 0.25f, 0.25f }, .pos = { 0.5f + (i % 4) * 1.0f, 0.25f, 4 + ((float)(i / 4)) * 2.0f }, .mass = 2 }, 0xD0C060);
    /* 0.9.7 a rope: 8 capsules on ball joints from the top of a post */
    add((arc_body_desc){ .type = ARC_BODY_STATIC, .shape = ARC_SHAPE_BOX, .size = { 0.15f, 3, 0.15f }, .pos = { -3, 3, 5 } }, 0x8A7050);
    int prev = 0; arc_quat sideways = { 0, 0, 0.70710678f, 0.70710678f };
    for (int i = 0; i < 8; i++) {
        int l = add((arc_body_desc){ .shape = ARC_SHAPE_CAPSULE, .size = { 0.08f, 0.15f }, .pos = { -2.73f + 0.46f * (float)i, 6, 5 }, .rot = sideways, .mass = 0.6f, .angular_damping = 0.4f }, 0xE0C080);
        arc_physics_add_joint(phys, &(arc_joint_desc){ .type = ARC_JOINT_BALL, .a = l, .b = prev, .anchor = { -2.96f + 0.46f * (float)i, 6, 5 } });
        prev = l;
    }
    /* a gate on a vertical hinge, limits -1.2 .. 1.2 rad, swung by its motor */
    add((arc_body_desc){ .type = ARC_BODY_STATIC, .shape = ARC_SHAPE_BOX, .size = { 0.12f, 0.8f, 0.12f }, .pos = { 7, 0.8f, -4 } }, 0x8A7050);
    int gate = add((arc_body_desc){ .shape = ARC_SHAPE_BOX, .size = { 1.0f, 0.6f, 0.06f }, .pos = { 8.15f, 0.8f, -4 }, .mass = 15 }, 0x70A0D0);
    G.gate_joint = arc_physics_add_joint(phys, &(arc_joint_desc){ .a = gate, .anchor = { 7.12f, 0.8f, -4 }, .axis = { 0, 1, 0 }, .limit = true, .lower = -1.2f, .upper = 1.2f, .motor = true, .motor_speed = 1.2f, .motor_torque = 60 });
    G.gate_dir = 1;
    s_hit = arc_sound_make(&(arc_sfx){ .wave = ARC_WAVE_NOISE, .freq = 600, .freq_end = 120, .sustain = 0.02f, .decay = 0.12f });
    cam = (arc_camera){ { 2, 7, 16 }, { -1, 1.5f, 0 }, { 0, 1, 0 }, 0.8f, 0, 0.1f, 200 };
    state0_n = arc_physics_save(phys, state0, sizeof state0); g0 = G;
}
static void tick(void) {
    if (arc_key_pressed(ARC_KEY_D)) G.debug = !G.debug;
    {   /* the gate: its motor turns it toward a limit, then back (G reverses it now) */
        float a = arc_physics_joint_angle(phys, G.gate_joint);
        if (arc_key_pressed(ARC_KEY_G) || (G.gate_dir > 0 && a > 1.15f) || (G.gate_dir < 0 && a < -1.15f)) G.gate_dir = -G.gate_dir;
        arc_physics_joint_motor(phys, G.gate_joint, 1.2f * G.gate_dir, 60);
    }
    if (arc_key_pressed(ARC_KEY_F)) drop_ragdoll();
    if (arc_key_pressed(ARC_KEY_R)) { arc_physics_load(phys, state0, state0_n); bool dbg = G.debug; G = g0; G.debug = dbg; }
    if (arc_key_pressed(ARC_MOUSE_LEFT) || arc_key_pressed(ARC_KEY_SPACE)) {
        arc_v3 o, d; arc_camera_ray(&cam, (float)arc_width(), (float)arc_height(), arc_mouse(), &o, &d);
        arc_physics_hit hit;
        shoot_toward(arc_physics_raycast(phys, o, d, 200, 0, &hit) ? hit.point : arc_v3_add(o, arc_v3_scale(d, 30)));
    }
    G.spin += arc_dt() * 1.2f;
    arc_physics_move_kinematic(phys, G.paddle, arc_v3_make(2, 0.3f, 5), arc_quat_axis_angle(arc_v3_make(0, 1, 0), G.spin));
    arc_physics_step(phys, arc_dt());
    arc_physics_event ev[32]; int n = arc_physics_events(phys, ev, 32);
    for (int i = 0; i < n && i < 32; i++) if (ev[i].impulse > 8) arc_play(s_hit, fminf(ev[i].impulse / 120.0f, 0.8f), 0);
}
static void draw(float alpha) {
    (void)alpha;
    arc_set_camera(&cam);
    arc_set_light(arc_v3_make(0.4f, 1, 0.5f), 0xFFF2E0, 0x404858);
    arc_set_shadows(arc_v3_make(-1, 0, 0), 14, 2048);
    arc_set_sky(0x3A5A90, 0x9AB0D0);
    arc_draw_mesh(m_floor, arc_m4_scale(arc_v3_make(40, 1, 40)), 0xFFFFFF);
    arc_draw_mesh(m_ramp, arc_m4_mul(arc_m4_translate(arc_v3_make(-7, 1.4f, -2)), arc_m4_mul(arc_m4_rotate_z(-0.35f), arc_m4_scale(arc_v3_make(8, 1, 4)))), 0xFFFFFF);
    for (int i = 0; i < G.n; i++) {
        int b = G.body[i]; arc_m4 m = arc_physics_matrix(phys, b); arc_v3 s = G.size[i];
        uint32_t tint = arc_physics_sleeping(phys, b) ? ((G.rgb[i] >> 1) & 0x7F7F7F) + 0x202020 : G.rgb[i];
        if (G.shape[i] == ARC_SHAPE_BOX) arc_draw_mesh(m_cube, arc_m4_mul(m, arc_m4_scale(arc_v3_scale(s, 2))), tint);
        else if (G.shape[i] == ARC_SHAPE_SPHERE) arc_draw_mesh(m_sphere, arc_m4_mul(m, arc_m4_scale(arc_v3_make(s.x * 2, s.x * 2, s.x * 2))), tint);
        else {   /* a capsule: a cylinder and two spheres */
            arc_draw_mesh(m_cyl, arc_m4_mul(m, arc_m4_scale(arc_v3_make(s.x * 2, s.y * 2, s.x * 2))), tint);
            arc_draw_mesh(m_sphere, arc_m4_mul(m, arc_m4_mul(arc_m4_translate(arc_v3_make(0, s.y, 0)), arc_m4_scale(arc_v3_make(s.x * 2, s.x * 2, s.x * 2)))), tint);
            arc_draw_mesh(m_sphere, arc_m4_mul(m, arc_m4_mul(arc_m4_translate(arc_v3_make(0, -s.y, 0)), arc_m4_scale(arc_v3_make(s.x * 2, s.x * 2, s.x * 2)))), tint);
        }
    }
    for (int i = 0; i < G.nrag; i++) arc_ragdoll_draw(phys, &G.rag[i], m_cyl, i & 1 ? 0xE09070 : 0xD0B0F0);
    if (G.debug) arc_physics_draw(phys, 0x40FF80);
    int awake = 0; for (int i = 0; i < G.n; i++) awake += !arc_physics_sleeping(phys, G.body[i]);
    arc_draw_textf(10, 10, 2, 0xFFFFFF, "%d bodies, %d awake (asleep ones are darker)   shots %d", G.n, awake, G.shots);
    arc_draw_text(10, 32, 2, 0xD0D8E8, "click / SPACE: shoot   F: ragdoll   R: reset   D: physics shapes   G: turn the gate back");
}
static void dump(arc_json *j) {
    int awake = 0; for (int i = 0; i < G.n; i++) awake += !arc_physics_sleeping(phys, G.body[i]);
    arc_v3 t = arc_physics_pos(phys, G.top);
    arc_json_num_k(j, "bodies", G.n); arc_json_num_k(j, "awake", awake); arc_json_num_k(j, "shots", G.shots);
    arc_json_num_k(j, "joints", arc_physics_joint_count(phys)); arc_json_num_k(j, "gate_angle", arc_physics_joint_angle(phys, G.gate_joint));
    arc_json_num_k(j, "ragdolls", G.nrag); if (G.nrag) arc_json_num_k(j, "ragdoll_head_y", arc_physics_pos(phys, G.rag[0].body[ARC_RAG_HEAD]).y);
    arc_json_arr_k(j, "top"); arc_json_num(j, t.x); arc_json_num(j, t.y); arc_json_num(j, t.z); arc_json_end(j);
}
static bool command(const char *name, int argc, const char **argv) {
    if (!strcmp(name, "shoot") && argc >= 3) { shoot_toward(arc_v3_make((float)atof(argv[0]), (float)atof(argv[1]), (float)atof(argv[2]))); return true; }
    if (!strcmp(name, "ragdoll")) { drop_ragdoll(); return true; }
    if (!strcmp(name, "reset")) { arc_physics_load(phys, state0, state0_n); G = g0; return true; }
    return false;
}
/* save_state: the game struct and the physics state together */
static size_t save_state(void *buf, size_t cap) {
    size_t pn = arc_physics_save(phys, NULL, 0), need = sizeof G + pn;
    if (cap >= need) { memcpy(buf, &G, sizeof G); arc_physics_save(phys, (uint8_t *)buf + sizeof G, pn); }
    return need;
}
static bool load_state(const void *buf, size_t size) {
    if (size < sizeof G) return false;
    memcpy(&G, buf, sizeof G);
    return arc_physics_load(phys, (const uint8_t *)buf + sizeof G, size - sizeof G);
}
arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "ARC physics", .width = 1100, .height = 650, .clear_color = 0x9AB0D0, .init = init, .tick = tick, .draw = draw,
                      .dump = dump, .command = command, .save_state = save_state, .load_state = load_state };
}
