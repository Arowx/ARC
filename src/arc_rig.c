/* ARC 0.10.0 camera rigs: top-down, follow, orbit, first person, fixed; lag, wall collision that pulls in at once
   and eases out, blends (orthographic <-> perspective through a matched view) and shake. Deterministic maths. */

static sweep_world RIGW = { { 0 }, { 0 }, 0, NULL, NULL, 1.0f, 0, 0 };
static struct { bool on; arc_cam_rig rig; bool have_game; arc_camera game; arc_cam_rig *live; uint64_t live_tick; arc_camera out; } CAMR;

void arc_cam_rig_set_world(arc_cworld cw, arc_physics phys, uint32_t phys_mask, bool (*grid_blocks)(int x, int z, void *user), void *user, float cell) {
    RIGW.cw = cw; RIGW.phys = phys; RIGW.phys_mask = phys_mask; RIGW.grid = grid_blocks; RIGW.user = user; RIGW.cell = cell > 0 ? cell : 1.0f;
}
static float rig_dist(const arc_cam_rig *r) { return r->kind == ARC_RIG_FOLLOW ? (r->distance > 0 ? r->distance : 5.0f) : (r->distance > 0 ? r->distance : 6.0f); }
static float rig_height(const arc_cam_rig *r) { return r->kind == ARC_RIG_TOPDOWN ? (r->height > 0 ? r->height : 30.0f) : (r->height != 0 ? r->height : 2.0f); }
static float rig_fov(const arc_cam_rig *r) { return r->fov_y > 0 ? r->fov_y : 1.0f; }
static float rig_ortho(const arc_cam_rig *r) { return r->ortho_half > 0 ? r->ortho_half : 8.0f; }
static float rig_k(float lag, float dt) { return lag <= 0 ? 1.0f : 1.0f - arc_expf(-3.0f * dt / lag); }
static float rig_wrap(float a) { const float P = 3.14159265f; while (a > P) a -= 2.0f * P; while (a < -P) a += 2.0f * P; return a; }
static arc_v3 rig_fwd(float yaw) { return arc_v3_make(arc_sinf(yaw), 0, arc_cosf(yaw)); }
static arc_v3 rig_pivot(const arc_cam_rig *r) { return arc_v3_make(r->target_now.x, r->target_now.y + r->aim_height, r->target_now.z); }
/* the wanted eye offset from the pivot (FOLLOW, ORBIT) */
static arc_v3 rig_offset(const arc_cam_rig *r) {
    if (r->kind == ARC_RIG_FOLLOW) { arc_v3 f = rig_fwd(r->yaw_now); float d = rig_dist(r); return arc_v3_make(-f.x * d, rig_height(r) - r->aim_height, -f.z * d); }
    float cp = arc_cosf(r->pitch), sp = arc_sinf(r->pitch), d = rig_dist(r);
    return arc_v3_make(-arc_sinf(r->yaw_now) * cp * d, sp * d, -arc_cosf(r->yaw_now) * cp * d);
}
static float rig_aspect(const arc_cam_rig *r) { if (r->aspect > 0) return r->aspect; int w = arc_width(), h = arc_height(); return w > 0 && h > 0 ? (float)w / (float)h : 16.0f / 9.0f; }
void arc_cam_rig_snap(arc_cam_rig *r) { if (r) r->started = false; }
void arc_cam_rig_update(arc_cam_rig *r, float dt) {
    if (!r) return;
    CAMR.live = r; CAMR.live_tick = arc_tick_count();
    float k = rig_k(r->follow_lag, dt);
    if (!r->started) { r->target_now = r->target; r->yaw_now = r->yaw; r->dist_now = -1; k = 1; r->started = true; }
    r->target_now = arc_v3_add(r->target_now, arc_v3_scale(arc_v3_sub(r->target, r->target_now), k));
    r->yaw_now = rig_wrap(r->yaw_now + rig_wrap(r->yaw - r->yaw_now) * k);
    r->shake_time += dt; r->shake -= dt * 1.2f; if (r->shake < 0) r->shake = 0;
    if (r->blend_left > 0) { r->blend_left -= dt; if (r->blend_left < 0) r->blend_left = 0; }
    switch (r->kind) {
    case ARC_RIG_FOLLOW: case ARC_RIG_ORBIT: {
        arc_v3 piv = rig_pivot(r), off = rig_offset(r); float L = sw_len(off);
        arc_v3 dir = L > 1e-6f ? arc_v3_scale(off, 1.0f / L) : arc_v3_make(0, 1, 0);
        float allowed = L;
        if (r->collide_radius > 0 && (RIGW.grid || RIGW.cw.id || RIGW.phys.id)) {
            sweep_hit h = sweep(&RIGW, piv, dir, L, r->collide_radius, 0, 7);
            if (h.hit) { allowed = h.t - 0.01f; if (allowed < 0) allowed = 0; }
        }
        if (r->dist_now < 0 || allowed <= r->dist_now) r->dist_now = allowed;   /* in at once */
        else r->dist_now += (allowed - r->dist_now) * rig_k(r->follow_lag > 0.25f ? r->follow_lag : 0.25f, dt);   /* out slowly */
        r->eye_now = arc_v3_add(piv, arc_v3_scale(dir, r->dist_now));
    } break;
    case ARC_RIG_TOPDOWN: {
        float hz = rig_ortho(r), hx = hz * rig_aspect(r), cx = r->target_now.x, cz = r->target_now.z;
        if (r->has_bounds) {
            float x0 = r->bounds[0] + hx, x1 = r->bounds[2] - hx, z0 = r->bounds[1] + hz, z1 = r->bounds[3] - hz;
            cx = x0 > x1 ? (r->bounds[0] + r->bounds[2]) * 0.5f : cx < x0 ? x0 : cx > x1 ? x1 : cx;
            cz = z0 > z1 ? (r->bounds[1] + r->bounds[3]) * 0.5f : cz < z0 ? z0 : cz > z1 ? z1 : cz;
        }
        r->eye_now = arc_v3_make(cx, r->target_now.y + rig_height(r), cz); r->dist_now = rig_height(r);
    } break;
    case ARC_RIG_FIRST: r->eye_now = r->target_now; r->dist_now = 0; break;
    case ARC_RIG_FIXED: r->eye_now = r->eye; r->dist_now = sw_len(arc_v3_sub(r->target_now, r->eye)); break;
    }
}
static arc_camera rig_base(const arc_cam_rig *r) {
    arc_camera c; memset(&c, 0, sizeof c);
    c.znear = r->znear > 0 ? r->znear : 0.1f; c.zfar = r->zfar > 0 ? r->zfar : 500.0f; c.up = arc_v3_make(0, 1, 0); c.fov_y = rig_fov(r);
    switch (r->kind) {
    case ARC_RIG_TOPDOWN:
        c.fov_y = 0; c.ortho_height = 2.0f * rig_ortho(r); c.eye = r->eye_now; c.target = arc_v3_make(r->eye_now.x, r->target_now.y, r->eye_now.z);
        c.up = rig_fwd(r->yaw_now); break;
    case ARC_RIG_FOLLOW: { arc_v3 f = rig_fwd(r->yaw_now); c.eye = r->eye_now; c.target = arc_v3_add(rig_pivot(r), arc_v3_scale(f, r->look_ahead)); } break;
    case ARC_RIG_ORBIT: c.eye = r->eye_now; c.target = rig_pivot(r); break;
    case ARC_RIG_FIRST: { float cp = arc_cosf(r->pitch); c.eye = r->eye_now; c.target = arc_v3_add(c.eye, arc_v3_make(arc_sinf(r->yaw_now) * cp, arc_sinf(r->pitch), arc_cosf(r->yaw_now) * cp)); } break;
    case ARC_RIG_FIXED: c.eye = r->eye_now; c.target = r->target_now; break;
    }
    if (sw_len(arc_v3_sub(c.target, c.eye)) < 1e-4f) c.target = arc_v3_add(c.eye, arc_v3_make(0, 0, 1));   /* (a FOLLOW pulled in all the way) */
    if (r->shake > 0) {
        float a = r->shake * r->shake, t = r->shake_time * 25.0f;
        arc_v3 o = arc_v3_make(arc_noise_value2(t, 0.5f, 101) * 2 - 1, arc_noise_value2(t, 1.5f, 202) * 2 - 1, arc_noise_value2(t, 2.5f, 303) * 2 - 1);
        float amp = c.fov_y > 0 ? 0.35f : 0.04f * c.ortho_height;
        c.eye = arc_v3_add(c.eye, arc_v3_scale(o, a * amp)); c.target = arc_v3_add(c.target, arc_v3_scale(o, a * amp * 0.6f));
    }
    return c;
}
static float rig_smooth(float t) { t = t < 0 ? 0 : t > 1 ? 1 : t; return t * t * (3 - 2 * t); }
static arc_v3 rig_lerp3(arc_v3 a, arc_v3 b, float t) { return arc_v3_add(a, arc_v3_scale(arc_v3_sub(b, a), t)); }
static arc_camera rig_mix(arc_camera a, arc_camera b, float s) {
    if (s <= 0) return a;
    if (s >= 1) return b;
    arc_camera c = b;
    arc_v3 ua = (a.up.x != 0 || a.up.y != 0 || a.up.z != 0) ? a.up : arc_v3_make(0, 1, 0), ub = (b.up.x != 0 || b.up.y != 0 || b.up.z != 0) ? b.up : arc_v3_make(0, 1, 0);
    c.up = sw_norm(rig_lerp3(ua, ub, s));
    if (c.up.x == 0 && c.up.y == 0 && c.up.z == 0) c.up = b.up;
    if (a.fov_y > 0 && b.fov_y > 0) {
        c.eye = rig_lerp3(a.eye, b.eye, s); c.target = rig_lerp3(a.target, b.target, s); c.fov_y = a.fov_y + (b.fov_y - a.fov_y) * s;
        c.znear = a.znear + (b.znear - a.znear) * s; c.zfar = a.zfar > b.zfar ? a.zfar : b.zfar; return c;
    }
    if (a.fov_y <= 0 && b.fov_y <= 0) {
        c.eye = rig_lerp3(a.eye, b.eye, s); c.target = rig_lerp3(a.target, b.target, s); c.ortho_height = a.ortho_height + (b.ortho_height - a.ortho_height) * s; return c;
    }
    /* orthographic <-> perspective: the same aim point, a direction turning, the height seen at the aim point
       changing smoothly, and a field of view opening from almost nothing (an orthographic view seen from far away) */
    const float NARROW = 0.03f;
    arc_v3 da = sw_norm(arc_v3_sub(a.target, a.eye)), db = sw_norm(arc_v3_sub(b.target, b.eye));
    float fa = a.fov_y > 0 ? a.fov_y : NARROW, fb = b.fov_y > 0 ? b.fov_y : NARROW;
    float ha = a.fov_y > 0 ? 2.0f * sw_len(arc_v3_sub(a.target, a.eye)) * arc_tanf(a.fov_y * 0.5f) : a.ortho_height;
    float hb = b.fov_y > 0 ? 2.0f * sw_len(arc_v3_sub(b.target, b.eye)) * arc_tanf(b.fov_y * 0.5f) : b.ortho_height;
    arc_v3 t = rig_lerp3(a.target, b.target, s), d = sw_norm(rig_lerp3(da, db, s));
    if (d.x == 0 && d.y == 0 && d.z == 0) d = db;
    float h = ha + (hb - ha) * s, f = arc_expf(arc_logf(fa) + (arc_logf(fb) - arc_logf(fa)) * s);
    float dist = h * 0.5f / arc_tanf(f * 0.5f);
    c.fov_y = f; c.ortho_height = 0; c.target = t; c.eye = arc_v3_sub(t, arc_v3_scale(d, dist));
    float zf = a.zfar > b.zfar ? a.zfar : b.zfar;
    c.znear = dist * 0.02f > 0.1f ? dist * 0.02f : 0.1f; c.zfar = dist + zf;
    return c;
}
arc_camera arc_cam_rig_camera(const arc_cam_rig *r) {
    arc_camera c; memset(&c, 0, sizeof c);
    if (!r) return c;
    c = rig_base(r);
    if (r->blend_left > 0 && r->blend_len > 0) c = rig_mix(r->blend_from, c, rig_smooth(1.0f - r->blend_left / r->blend_len));
    return c;
}
void arc_cam_rig_blend(arc_cam_rig *from, const arc_cam_rig *to, float seconds) {
    if (!from || !to) return;
    arc_camera now = arc_cam_rig_camera(from);
    bool had = from->started; float shake = from->shake, st = from->shake_time; arc_v3 tn = from->target_now; float yn = from->yaw_now;
    *from = *to;
    from->shake = shake; from->shake_time = st;
    if (had) { from->target_now = tn; from->yaw_now = yn; from->started = true; from->dist_now = -1; }   /* carries on from where it was */
    if (had && from->kind == ARC_RIG_FIRST) from->yaw_now = from->yaw;
    if (seconds > 0 && had) { from->blend_from = now; from->blend_len = seconds; from->blend_left = seconds; }
    else { from->blend_left = 0; from->blend_len = 0; }
}
bool arc_cam_rig_blending(const arc_cam_rig *r) { return r && r->blend_left > 0; }
void arc_cam_rig_shake(arc_cam_rig *r, float amount) { if (!r) return; r->shake += amount; if (r->shake > 1) r->shake = 1; if (r->shake < 0) r->shake = 0; }

/* ---- the `cam` command and frame.camera ---- */
static const char *RIG_NAMES[] = { "topdown", "follow", "orbit", "first", "fixed" };
static arc_cam_rig *rig_live(void) { return CAMR.live && arc_tick_count() - CAMR.live_tick <= 2 ? CAMR.live : NULL; }
/* the command's own rig follows the game camera's aim: moved each tick, drawn in place of the game camera */
static void rig_follow_game(void) {
    CAMR.rig.target = CAMR.game.target;
    arc_v3 d = arc_v3_sub(CAMR.game.target, CAMR.game.eye); if (d.x * d.x + d.z * d.z > 1e-8f) CAMR.rig.yaw = arc_atan2f(d.x, d.z);
}
static void rig_update_keep_live(float dt) { arc_cam_rig *keep = CAMR.live; uint64_t kt = CAMR.live_tick; arc_cam_rig_update(&CAMR.rig, dt); CAMR.live = keep; CAMR.live_tick = kt; }
void arc__rig_tick(void) { if (CAMR.on && CAMR.have_game) { rig_follow_game(); rig_update_keep_live(arc_dt()); } }
const arc_camera *arc__rig_override(const arc_camera *game) {
    if (!CAMR.on) return game;
    CAMR.game = *game; CAMR.have_game = true;
    if (!CAMR.rig.started) { rig_follow_game(); rig_update_keep_live(0); }
    CAMR.out = arc_cam_rig_camera(&CAMR.rig); return &CAMR.out;
}
static void rig_print(const char *who, const arc_cam_rig *r) {
    arc_camera c = arc_cam_rig_camera(r);
    printf("ARC cam %s kind=%s eye=%.3f,%.3f,%.3f target=%.3f,%.3f,%.3f dist=%.3f shake=%.2f blending=%d\n", who, RIG_NAMES[(int)r->kind % 5],
           c.eye.x, c.eye.y, c.eye.z, c.target.x, c.target.y, c.target.z, r->dist_now, r->shake, arc_cam_rig_blending(r) ? 1 : 0);
}
void arc__rig_command(int argc, const char **argv, const arc_camera *game_cam) {
    arc_cam_rig *live = rig_live();
    if (argc == 1) {
        if (live) rig_print("game", live);
        if (CAMR.on) rig_print("override", &CAMR.rig);
        if (!live && !CAMR.on) printf("ARC cam no rig in use (cam rig KIND puts one over the game camera)\n");
        fflush(stdout); return;
    }
    if (!strcmp(argv[1], "shake") && argc >= 3) {
        arc_cam_rig *r = live ? live : CAMR.on ? &CAMR.rig : NULL;
        if (!r) { printf("ARC error cam shake: no rig in use\n"); fflush(stdout); return; }
        arc_cam_rig_shake(r, (float)atof(argv[2])); printf("ARC ok cam shake %.2f\n", r->shake); fflush(stdout); return;
    }
    if (!strcmp(argv[1], "rig") && argc >= 3) {
        if (!strcmp(argv[2], "off")) { CAMR.on = false; printf("ARC ok cam rig off\n"); fflush(stdout); return; }
        int kind = -1; for (int k = 0; k < 5; k++) if (!strcmp(argv[2], RIG_NAMES[k])) kind = k;
        if (!strcmp(argv[2], "third")) kind = ARC_RIG_FOLLOW;
        if (kind < 0) { printf("ARC error cam rig: topdown, follow, orbit, first, fixed or off\n"); fflush(stdout); return; }
        if (live && !CAMR.on) {   /* the game's rig blends to the other kind */
            arc_cam_rig to = *live; to.kind = (arc_rig_kind)kind; arc_cam_rig_blend(live, &to, 0.4f);
            printf("ARC ok cam rig %s (the game's rig)\n", RIG_NAMES[kind]); fflush(stdout); return;
        }
        arc_cam_rig to; memset(&to, 0, sizeof to); to.kind = (arc_rig_kind)kind; to.follow_lag = 0.15f;
        if (game_cam) { to.target = game_cam->target; arc_v3 d = arc_v3_sub(game_cam->target, game_cam->eye); if (d.x * d.x + d.z * d.z > 1e-8f) to.yaw = arc_atan2f(d.x, d.z); }
        if (game_cam) { CAMR.game = *game_cam; CAMR.have_game = true; }
        if (CAMR.on) arc_cam_rig_blend(&CAMR.rig, &to, 0.4f);
        else {
            CAMR.rig = to;
            if (CAMR.have_game) { rig_follow_game(); rig_update_keep_live(0); CAMR.rig.blend_from = CAMR.game; CAMR.rig.blend_len = CAMR.rig.blend_left = 0.4f; }
        }
        CAMR.on = true; printf("ARC ok cam rig %s (over the game camera)\n", RIG_NAMES[kind]); fflush(stdout); return;
    }
    printf("ARC error cam: use 'cam', 'cam rig topdown|follow|orbit|first|fixed|off' or 'cam shake N'\n"); fflush(stdout);
}
void arc__rig_json(arc_json *j) {   /* inside frame.camera */
    const arc_cam_rig *r = CAMR.on ? &CAMR.rig : rig_live();
    if (!r) return;
    arc_json_obj_k(j, "rig");
    arc_json_str_k(j, "kind", RIG_NAMES[(int)r->kind % 5]); arc_json_bool_k(j, "override", CAMR.on);
    arc_json_num_k(j, "dist", r->dist_now); arc_json_num_k(j, "shake", r->shake); arc_json_bool_k(j, "blending", arc_cam_rig_blending(r));
    arc_json_end(j);
}
