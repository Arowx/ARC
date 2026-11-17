/* ARC 0.5 self-test checks: included by test_core.c (it provides CHECK, feq, near8). */

/* ---- particles ---- */
static arc_texture tgt5;
static arc_emitter tem;
static const arc_fx fx_still = { .life = 100, .size = 1.5f, .color = 0xFF0000, .alpha = 1, .alpha_end = 1, .additive = true };
static const arc_fx fx_stream = { .life = 100, .speed = 1, .spread = 0.3f, .rate = 60, .gravity = { 0, -9.8f, 0 } };
static int v05_count_at_4;
static void v05_tick(int frame) {
    if (frame == 1) {
        arc_fx_burst(&fx_still, arc_v3_make(0, 0, 0), arc_v3_make(0, 1, 0), 1);
        tem = arc_emitter_create(&fx_stream, arc_v3_make(100, 0, 0), arc_v3_make(0, 1, 0));
        CHECK(arc_particle_count() == 1 && arc_emitter_alive(tem));
    }
    if (frame == 4) v05_count_at_4 = arc_particle_count();
    if (frame == 6) {
        CHECK(v05_count_at_4 >= 3 && arc_particle_count() == v05_count_at_4 + 2);   /* 60 per second = one per tick */
        arc_emitter_destroy(tem); CHECK(!arc_emitter_alive(tem));
    }
    if (frame == 8) CHECK(arc_particle_count() == v05_count_at_4 + 2);            /* destroyed emitter: particles stay */
}
static void v05_draw_target(void) {
    if (!tgt5.id) tgt5 = arc_target_create(16, 16, false);
    arc_target_begin(tgt5, true, 0xFF000000);
    arc_camera cam = { { 0, 0, 5 }, { 0, 0, 0 }, { 0, 1, 0 }, 0, 2.0f, 0.1f, 10 };
    arc_set_camera(&cam);
    arc_draw_particles();
    arc_target_end();
}
static const uint8_t *t5px(int x, int y) { return arc__textures[tgt5.id].rgba + ((size_t)y * 16 + x) * 4; }
static void v05_check(void) {
    const uint8_t *c = t5px(8, 8), *k = t5px(0, 0);
    CHECK(c[0] > 200 && c[1] < 10 && c[2] < 10);    /* the dot's middle: bright red (unlit, additive) */
    CHECK(k[0] < 10);                                /* outside the dot: still black */
    CHECK(t5px(8, 3)[0] < c[0]);                     /* soft falloff toward the edge */
}

/* ---- collision ---- */
static void v05_collision(void) {
    arc_hit h;
    CHECK(arc_ray_sphere(arc_v3_make(0, 0, -5), arc_v3_make(0, 0, 1), arc_v3_make(0, 0, 0), 1, 100, &h) && feq(h.t, 4) && feq(h.normal.z, -1));
    CHECK(!arc_ray_sphere(arc_v3_make(0, 2, -5), arc_v3_make(0, 0, 1), arc_v3_make(0, 0, 0), 1, 100, NULL));
    CHECK(arc_ray_box(arc_v3_make(-5, 0.5f, 0.5f), arc_v3_make(2, 0, 0), arc_v3_make(0, 0, 0), arc_v3_make(1, 1, 1), 100, &h) && feq(h.t, 2.5f) && feq(h.normal.x, -1));
    CHECK(arc_ray_triangle(arc_v3_make(0.2f, 1, 0.2f), arc_v3_make(0, -1, 0), arc_v3_make(0, 0, 0), arc_v3_make(0, 0, 1), arc_v3_make(1, 0, 0), 10, &h) && feq(h.t, 1) && feq(h.normal.y, 1));
    arc_mesh cube = arc_mesh_cube(0xFFFFFF);
    CHECK(arc_ray_mesh(cube, arc_m4_translate(arc_v3_make(0, 0, 3)), arc_v3_make(0, 0, 0), arc_v3_make(0, 0, 1), 10, &h) && feq(h.t, 2.5f) && feq(h.normal.z, -1));
    arc_cworld w = arc_cworld_create();
    arc_cworld_add_box(w, arc_v3_make(-10, -1, -10), arc_v3_make(10, 0, 10), 1);     /* floor */
    arc_cworld_add_box(w, arc_v3_make(2, 0, -10), arc_v3_make(3, 3, 10), 2);          /* wall at x = 2 */
    arc_cworld_add_box(w, arc_v3_make(-8, 0, -10), arc_v3_make(-2, 0.25f, 10), 3);    /* a step west of x = -2 */
    arc_cworld_add_mesh(w, cube, arc_m4_trs_y(arc_v3_make(0, 5, 5), 0, arc_v3_make(1, 1, 1)), 4);
    CHECK(arc_cworld_triangles(w) == 48);
    CHECK(arc_cworld_raycast(w, arc_v3_make(0, 5, 0), arc_v3_make(0, -1, 0), 100, &h) && feq(h.t, 5) && feq(h.normal.y, 1) && h.user == 1);
    CHECK(arc_cworld_raycast(w, arc_v3_make(0, 1, 0), arc_v3_make(1, 0, 0), 100, &h) && feq(h.t, 2) && h.user == 2 && feq(h.normal.x, -1));
    CHECK(arc_cworld_raycast(w, arc_v3_make(0, 5, 0), arc_v3_make(0, 0, 1), 100, &h) && feq(h.t, 4.5f) && h.user == 4);
    CHECK(!arc_cworld_raycast(w, arc_v3_make(0, 5, 0), arc_v3_make(0, 1, 0), 100, NULL));
    arc_v3 sp = arc_v3_make(0, 0.1f, 0);
    CHECK(arc_cworld_push_sphere(w, &sp, 0.5f) >= 1 && fabsf(sp.y - 0.5f) < 1e-3f);
    CHECK(arc_cworld_overlap_sphere(w, arc_v3_make(1.8f, 1, 0), 0.3f) && !arc_cworld_overlap_sphere(w, arc_v3_make(1, 1, 0), 0.3f));
    /* character: fall, land, walk into the wall, climb the step */
    arc_character ch = { .pos = { 0, 1, 0 }, .radius = 0.3f, .height = 1.8f, .step_height = 0.3f, .gravity = 20 };
    for (int i = 0; i < 60; i++) arc_character_move(w, &ch, 1.0f / 60);
    CHECK(ch.on_ground && fabsf(ch.pos.y) < 0.01f);
    for (int i = 0; i < 90; i++) { ch.velocity.x = 3; ch.velocity.z = 0; arc_character_move(w, &ch, 1.0f / 60); }
    CHECK(ch.hit_wall && fabsf(ch.pos.x - 1.7f) < 0.02f && ch.on_ground && fabsf(ch.pos.y) < 0.01f);
    for (int i = 0; i < 120; i++) { ch.velocity.x = -3; arc_character_move(w, &ch, 1.0f / 60); }
    CHECK(ch.pos.x < -3 && fabsf(ch.pos.y - 0.25f) < 0.02f && ch.on_ground);
    for (int i = 0; i < 120; i++) { ch.velocity.x = 3; arc_character_move(w, &ch, 1.0f / 60); }   /* back down the step: sticks */
    CHECK(fabsf(ch.pos.y) < 0.02f && ch.on_ground);
    ch.velocity.y = 6; ch.velocity.x = 0; arc_character_move(w, &ch, 1.0f / 60);
    CHECK(!ch.on_ground && ch.pos.y > 0.05f);                                                    /* a jump leaves the ground */
    arc_cworld_destroy(w); CHECK(!arc_cworld_raycast(w, arc_v3_make(0, 5, 0), arc_v3_make(0, -1, 0), 100, NULL));
    arc_mesh_destroy(cube);
}

void arc__audio_tick(void);
/* ---- positional audio, filters, reverb (drives the mixer directly) ---- */
static void v05_audio(void) {
    extern bool arc__audio_test_mode; arc__audio_test_mode = true;
    arc__audio_init(44100); arc_set_master_volume(1);
    static int16_t dc[44100]; for (int i = 0; i < 44100; i++) dc[i] = 16384;   /* half-scale DC, 1 s */
    arc_sound s = arc_sound_from_pcm(dc, 44100, 44100);
    static float out[2 * 4096];
    arc_set_listener(arc_v3_make(0, 0, 0), arc_v3_make(0, 0, -1), arc_v3_make(0, 1, 0));
    arc_voice v = arc_play_3d(s, arc_v3_make(5, 0, 0), 1); arc__audio_mix(out, 64);
    CHECK(out[2 * 10 + 1] > out[2 * 10] * 3 && out[2 * 10 + 1] > 0.05f);           /* to the right: right ear louder */
    float near_r = out[2 * 10 + 1];
    arc_voice_set_position(v, arc_v3_make(-20, 0, 0)); arc__audio_tick(); arc__audio_mix(out, 64);
    CHECK(out[2 * 10] > out[2 * 10 + 1] * 3 && out[2 * 10] < near_r);               /* moved left and further: quieter, left ear */
    arc_voice_set_position(v, arc_v3_make(0, 0, -60)); arc__audio_tick(); arc__audio_mix(out, 64);
    CHECK(out[2 * 10] == 0 && out[2 * 10 + 1] == 0);                                 /* beyond max distance: silent */
    arc_set_listener(arc_v3_make(0, 0, -59.5f), arc_v3_make(0, 0, -1), arc_v3_make(0, 1, 0)); arc__audio_tick(); arc__audio_mix(out, 64);
    CHECK(fabsf(out[2 * 10] - out[2 * 10 + 1]) < 1e-3f && out[2 * 10] > 0.3f);     /* the listener walked up to it: loud, centred */
    arc_voice_stop(v);
    arc_voice w = arc_play(s, 1, 0); arc_voice_set_lowpass(w, 100); arc__audio_mix(out, 4096);
    CHECK(out[0] < 0.05f && fabsf(out[2 * 4000] - 0.5f) < 0.01f);                  /* low-pass: a slow rise to the DC level */
    arc_voice_stop(w); arc__audio_mix(out, 16);
    static int16_t click[64]; for (int i = 0; i < 64; i++) click[i] = 20000;
    arc_sound c = arc_sound_from_pcm(click, 64, 44100);
    arc_play(c, 1, 0); arc__audio_mix(out, 4096); CHECK(out[2 * 3000] == 0);       /* dry: nothing after the click */
    arc_set_reverb(0.8f, 0.7f);
    arc_play(c, 1, 0); arc__audio_mix(out, 4096);
    float tail = 0; for (int i = 1500; i < 4096; i++) tail += fabsf(out[i * 2]);
    CHECK(tail > 0.05f);                                                             /* reverb: a tail */
    arc_set_reverb(0, 0); arc_set_master_lowpass(0);
    arc_set_master_volume(0.8f); arc__audio_test_mode = false;
}

/* ---- UI: a panel every tick, driven by raw mouse and keys (they arrive on the next tick) ---- */
static int ui_clicks, ui_click_frame[4]; static float ui_vol; static bool ui_on;
bool arc__ui_find(const char *label, float *cx, float *cy);
static void v05_ui(int frame) {
    arc_ui_begin("Test panel", 10, 10, 200);
    if (arc_ui_button("Go")) { if (ui_clicks < 4) ui_click_frame[ui_clicks] = frame; ui_clicks++; }
    arc_ui_slider("Vol", &ui_vol, 0, 1);
    arc_ui_toggle("On", &ui_on);
    arc_ui_end();
    float x, y;
    if (frame == 2) { CHECK(arc__ui_find("go", &x, &y)); arc__input_mouse(x, y); arc__input_key(ARC_MOUSE_LEFT, true); }
    if (frame == 3) arc__input_key(ARC_MOUSE_LEFT, false);
    if (frame == 5) {                                                 /* press on the slider, 3/4 of the way along */
        CHECK(arc__ui_find("Vol", &x, &y)); float w = 200 - 12;
        arc__input_mouse(10 + 6 + w * 0.75f, y); arc__input_key(ARC_MOUSE_LEFT, true);
    }
    if (frame == 6) { CHECK(fabsf(ui_vol - 0.75f) < 0.01f); arc__input_key(ARC_MOUSE_LEFT, false); CHECK(arc_ui_wants_mouse()); }
    if (frame == 7) { arc__input_key(ARC_KEY_DOWN, true); arc__input_key(ARC_KEY_DOWN, false); }   /* keyboard: focus the first widget */
    if (frame == 8) { arc__input_key(ARC_KEY_ENTER, true); arc__input_key(ARC_KEY_ENTER, false); } /* ...and press it */
    if (frame == 9) CHECK(ui_clicks == 2 && ui_click_frame[0] == 4 && ui_click_frame[1] == 9 && !ui_on);
}

/* ---- snapshot writer/reader ---- */
static void v05_state_io(void) {
    arc_pool p; arc_pool_init(&p, arc_perm(), sizeof(int), 8);
    arc_handle h1 = arc_pool_add(&p); *(int *)arc_pool_get(&p, h1) = 42;
    arc_handle h2 = arc_pool_add(&p); *(int *)arc_pool_get(&p, h2) = 7;
    uint8_t small[8]; arc_writer w0 = { small, sizeof small, 0 };
    arc_write_pool(&w0, &p); CHECK(w0.len > sizeof small);                 /* counts past the end: the size needed */
    static uint8_t buf[1024]; arc_writer w = { buf, sizeof buf, 0 };
    float f = 3.5f; arc_write(&w, &f, sizeof f); arc_write_pool(&w, &p);
    arc_pool_remove(&p, h1); *(int *)arc_pool_get(&p, h2) = 9; arc_pool_add(&p);
    arc_reader r = { buf, w.len, 0, false }; float g = 0;
    CHECK(arc_read(&r, &g, sizeof g) && g == 3.5f && arc_read_pool(&r, &p) && !r.fail);
    CHECK(p.count == 2 && arc_pool_get(&p, h1) && *(int *)arc_pool_get(&p, h1) == 42 && *(int *)arc_pool_get(&p, h2) == 7);
    arc_reader r2 = { buf, 3, 0, false }; CHECK(!arc_read(&r2, &g, sizeof g) && r2.fail);   /* short data fails safely */
}

/* ---- asset packs (build.sh test makes build/test.arcpak from build/packsrc/inpack) ---- */
static void v05_pack(void) {
    CHECK(!arc_file_exists("inpack/pyr.arcm"));
    CHECK(arc_pack_mount("test.arcpak"));
    CHECK(arc_file_exists("inpack/pyr.arcm") && arc_file_exists("./inpack//hello.txt") && !arc_file_exists("inpack/nope"));
    arc_mesh m = arc_mesh_load("inpack/pyr.arcm"); CHECK(m.id && arc_mesh_triangles(m) == 6);
    size_t n = 0; uint8_t *t = arc_file_read("inpack/hello.txt", arc_frame(), &n);
    CHECK(t && n == 550 && !memcmp(t, "hello pack hello pack", 21));   /* stored compressed: inflated on read */
    CHECK(!arc_pack_mount("pyramid.arcm"));                              /* not a pack */
}

/* ---- shadows (into a target, so every renderer can be checked the same way) and post-processing ---- */
static arc_texture tgt6;
static void v05_draw_shadow_target(void) {
    if (!tgt6.id) tgt6 = arc_target_create(64, 64, false);
    arc_target_begin(tgt6, true, 0xFF000000);
    arc_camera cam = { { 0, 10, 0 }, { 0, 0, 0 }, { 0, 0, -1 }, 0, 4.0f, 0.1f, 20 };
    arc_set_camera(&cam);
    arc_set_light(arc_v3_make(1, 1, 0), 0xFFFFFF, 0x333333);
    arc_set_fog(0, 0, 0); arc_set_rim(0, 3);
    arc_set_shadows(arc_v3_make(0, 0, 0), 4, 512);
    arc_draw_box(arc_v3_make(-2, -0.1f, -2), arc_v3_make(2, 0, 2), 0xFFFFFF);          /* ground */
    arc_draw_box(arc_v3_make(-0.5f, 1, -0.5f), arc_v3_make(0.5f, 2, 0.5f), 0xFFFFFF);   /* a block floating above it */
    arc_target_end();
    arc_set_shadows(arc_v3_make(0, 0, 0), 0, 0);      /* (lights and shadows belong to the pass: switch off after it ends) */
    arc_set_light(arc_v3_make(0.4f, 1.0f, 0.3f), 0xFFFFFF, 0x404850);
}
static const uint8_t *t6px(int x, int y) { return arc__textures[tgt6.id].rgba + ((size_t)y * 64 + x) * 4; }
static void v05_check_shadows(void) {
    const uint8_t *sh = t6px(8, 32), *lit = t6px(56, 32);
    CHECK(abs(sh[0] - 51) <= 4 && abs(lit[0] - 231) <= 4);      /* ground in the block's shadow: ambient only; beside it: lit */
}
/* post: exposure 3 on the 3D (the clear colour 0B0E14 triples), 2D on top untouched */
static void v05_draw_post(void) { arc_post p = { .exposure = 3, .tilt_shift = 1 };   /* the corner checked below is fully blurred: blur must keep the brightness */ arc_set_post(&p); arc_draw_rect(310, 170, 10, 10, 0xFFFFFF); }
static void v05_check_post(void) {
    int w, h; uint8_t *px = arc_image_load("arc_test_shot.png", &w, &h);
    CHECK(px && w == 320);
    if (px) {
        const uint8_t *bg = px + ((size_t)178 * w + 2) * 4, *ui = px + ((size_t)175 * w + 315) * 4;
        CHECK(abs(bg[0] - 33) <= 2 && abs(bg[1] - 42) <= 2 && abs(bg[2] - 60) <= 2);
        CHECK(ui[0] == 255 && ui[1] == 255 && ui[2] == 255);
        free(px);
    }
    arc_set_post(NULL);
}

/* ---- review fixes: hostile packs, UI overflow, voices made positional, particle snapshots ---- */
size_t arc__fx_state(arc_writer *w); bool arc__fx_state_read(arc_reader *r, bool commit); void arc__fx_tick(float dt);
static void v05_review(void) {
    uint8_t bad[35] = { 'A', 'R', 'C', 'P', 1, 0, 0, 0, 1, 0, 0, 0, 15, 0, 0, 0 };   /* one entry: stored 4 bytes, claims 100000 */
    uint32_t off = 31, st = 4, sz = 100000; memcpy(bad + 16, &off, 4); memcpy(bad + 20, &st, 4); memcpy(bad + 24, &sz, 4); bad[28] = 0; bad[29] = 1; bad[30] = 'x';
    CHECK(!arc_pack_mount_memory(bad, sizeof bad, "bad"));
    bad[29] = 200; sz = 4; memcpy(bad + 24, &sz, 4);                     /* a name running past the directory */
    CHECK(!arc_pack_mount_memory(bad, sizeof bad, "bad2"));
    arc_ui_begin("Big", 0, 0, 100); for (int i = 0; i < 2100; i++) arc_ui_button(arc_fmt("b%d", i)); arc_ui_end();
    arc_ui_begin("After", 0, 0, 100); arc_ui_label("x"); arc_ui_end();      /* full command buffer: no write past it */
    extern bool arc__audio_test_mode; arc__audio_test_mode = true;
    static int16_t dc[4410]; for (int i = 0; i < 4410; i++) dc[i] = 8000;
    arc_sound s = arc_sound_from_pcm(dc, 4410, 44100);
    arc_set_listener(arc_v3_make(0, 0, 0), arc_v3_make(0, 0, -1), arc_v3_make(0, 1, 0));
    arc_voice v = arc_play(s, 1, 0); arc_voice_set_position(v, arc_v3_make(0, 0, 0)); arc_voice_set_volume(v, 0.5f);
    static float out[2 * 64]; arc__audio_mix(out, 64);
    CHECK(out[20] == out[20] && out[20] > 0.05f);                          /* positional at the listener: no NaN, audible */
    arc_voice_stop(v); arc__audio_mix(out, 8); arc__audio_test_mode = false;
    /* particles: snapshot, run, restore, run again: identical */
    static const arc_fx fz = { .life = 2, .speed = 3, .spread = 1, .rate = 120, .gravity = { 0, -5, 0 } };
    arc_particles_clear(); arc_emitter e = arc_emitter_create(&fz, arc_v3_make(0, 0, 0), arc_v3_make(0, 1, 0));
    for (int i = 0; i < 30; i++) arc__fx_tick(1.0f / 60);
    static uint8_t snap[1 << 20], a[1 << 20], b[1 << 20]; arc_writer w = { snap, sizeof snap, 0 }; arc__fx_state(&w);
    for (int i = 0; i < 30; i++) arc__fx_tick(1.0f / 60);
    arc_writer wa = { a, sizeof a, 0 }; arc__fx_state(&wa);
    arc_reader r = { snap, w.len, 0, false }; CHECK(arc__fx_state_read(&r, true));
    for (int i = 0; i < 30; i++) arc__fx_tick(1.0f / 60);
    arc_writer wb = { b, sizeof b, 0 }; arc__fx_state(&wb);
    CHECK(wa.len == wb.len && !memcmp(a, b, wa.len));
    arc_emitter_destroy(e); arc_particles_clear();
}
