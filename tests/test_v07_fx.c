/* ARC 0.7 effects self-test (included by test_core.c after test_v06.c): flipbook particles, soft particles,
   sub-emitters, solid collisions, trails and decals. v07fx_draw() draws into a 96 x 32 target once (headless runs draw
   only for the shot, after tick 5): it sets the particles aside (state save), ages its own with arc__fx_tick() and puts
   the others back, so test_v05.c's particle counts are untouched. v07fx_check() reads the target at tick 9 and runs the
   simulation checks directly with arc__fx_tick(). */
static arc_texture tgt70, tex70_row, tex70_grid, tex70_white; static arc_mesh q70;
static const uint32_t V07_RGB[4] = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF };
static arc_texture v07_sheet(int fx, int fy) {   /* fx * fy frames of 2 x 2 pixels, colours V07_RGB in reading order */
    uint8_t px[8 * 4 * 4]; int w = fx * 2, h = fy * 2;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        uint32_t c = V07_RGB[(y / 2) * fx + x / 2]; uint8_t *p = px + (y * w + x) * 4;
        p[0] = (uint8_t)(c >> 16); p[1] = (uint8_t)(c >> 8); p[2] = (uint8_t)c; p[3] = 255;
    }
    return arc_texture_create(w, h, px, false);
}
/* target pixels: x = (wx - 300) * 8 + 48, y = 16 - wy * 8 (orthographic, 12 x 4 units) */
static const uint8_t *t70fxpx(int x, int y) { return arc__textures[tgt70.id].rgba + ((size_t)y * 96 + x) * 4; }
static void v07fx_draw(void) {
    if (tgt70.id) return;
    static uint8_t saved[1 << 20]; arc_writer sw = { saved, sizeof saved, 0 }; arc__fx_state(&sw);
    arc_particles_clear();
    tgt70 = arc_target_create(96, 32, false);
    tex70_row = v07_sheet(4, 1); tex70_grid = v07_sheet(2, 2);
    uint8_t w[16]; memset(w, 255, sizeof w); tex70_white = arc_texture_create(2, 2, w, false);
    q70 = m6_quad(0xFFFFFF);
    /* flipbook: A plays 4 frames at 15 fps (4 ticks a frame), aged 6 ticks -> frame 1 (green); B plays a 2 x 2 sheet once
       over 0.2 s (12 ticks), aged 8 ticks -> t 0.67 -> frame 2 (blue, row 1 column 0) */
    arc_fx fb = { .life = 0.2f, .size = 1, .alpha = 1, .alpha_end = 1, .frames_x = 2, .frames_y = 2, .texture = tex70_grid };
    arc_fx fa = { .life = 100, .size = 1, .alpha = 1, .alpha_end = 1, .frames_x = 4, .fps = 15, .texture = tex70_row };
    arc_fx_burst(&fb, arc_v3_make(297, -0.5f, 0), arc_v3_make(0, 1, 0), 1);
    for (int i = 0; i < 2; i++) arc__fx_tick(arc_dt());
    arc_fx_burst(&fa, arc_v3_make(295, -0.5f, 0), arc_v3_make(0, 1, 0), 1);
    for (int i = 0; i < 6; i++) arc__fx_tick(arc_dt());
    /* soft: white quads 0.1 in front of a black wall; soft 0.4 lets a quarter through, the hard one is solid */
    arc_fx fs = { .life = 100, .size = 1, .alpha = 1, .alpha_end = 1, .soft = 0.4f, .texture = tex70_white }, fh = fs; fh.soft = 0;
    arc_fx_burst(&fs, arc_v3_make(299, -0.5f, 0.1f), arc_v3_make(0, 1, 0), 1);
    arc_fx_burst(&fh, arc_v3_make(301, -0.5f, 0.1f), arc_v3_make(0, 1, 0), 1);
    /* decals: one out of sight, a green one, 254 more out of sight (the pool is full), one more (replaces the first),
       then the red one replaces the oldest left: the green */
    arc_decals_clear();
    arc_decal_add((arc_texture){ 0 }, arc_v3_make(-400, 0, -1), arc_v3_make(0, 1, 0), 1, 0, 0xFFFFFF, 0);
    arc_decal_add(tex70_white, arc_v3_make(305.2f, -1.4f, 0), arc_v3_make(0, 0, 1), 0.8f, 0, 0x00FF00, 0);
    for (int i = 0; i < ARC_MAX_DECALS - 1; i++) arc_decal_add((arc_texture){ 0 }, arc_v3_make(-400, 0, (float)i), arc_v3_make(0, 1, 0), 1, 0, 0xFFFFFF, 0);
    arc_decal_add(tex70_white, arc_v3_make(303.3f, -0.5f, 0), arc_v3_make(0, 0, 1), 1.2f, 0.4f, 0xFF0000, 0);
    /* trail: x 295 -> 305 along y = 1.5, a point every 0.5 (one per tick), life 0.4 s: the tail has faded most */
    static arc_trail tr; tr.life = 0.4f;
    for (int i = 0; i <= 20; i++) arc_trail_push(&tr, arc_v3_make(295 + 0.5f * (float)i, 1.5f, 0));
    arc_target_begin(tgt70, true, 0xFF000000);
    arc_camera cam = { { 300, 0, 5 }, { 300, 0, 0 }, { 0, 1, 0 }, 0, 4.0f, 0.1f, 10 };
    arc_set_camera(&cam); arc_set_light(arc_v3_make(0, 0, 1), 0x000000, 0xFFFFFF); arc_set_fog(0, 0, 0); arc_set_rim(0, 3);
    arc_draw_mesh(q70, arc_m4_trs_y(arc_v3_make(300, -0.5f, 0), 0, arc_v3_make(3, 2, 1)), 0x000000);     /* black wall */
    arc_draw_mesh(q70, arc_m4_trs_y(arc_v3_make(304, -0.5f, 0), 0, arc_v3_make(4, 2.6f, 1)), 0x808080);  /* grey wall */
    arc_draw_particles();
    arc_draw_decals();
    arc_trail_style ts = { 0 }; ts.width = 0.5f; ts.color = 0x00FF00; ts.additive = true;
    arc_trail_draw(&tr, &ts);
    arc_target_end();
    arc_set_light(arc_v3_make(0.4f, 1.0f, 0.3f), 0xFFFFFF, 0x404850);
    arc_reader sr = { saved, sw.len, 0, false }; CHECK(arc__fx_state_read(&sr, true));   /* the other tests' particles back */
    arc_decals_clear();   /* lifetimes: one for ever, then 255 lasting 0.05 s (3 ticks); counted at tick 9 */
    arc_decal_add((arc_texture){ 0 }, arc_v3_make(-500, 0, 0), arc_v3_make(0, 1, 0), 1, 0, 0xFFFFFF, 0);
    for (int i = 0; i < ARC_MAX_DECALS - 1; i++) arc_decal_add((arc_texture){ 0 }, arc_v3_make(-500, 0, 2), arc_v3_make(0, 1, 0), 1, 0, 0xFFFFFF, 0.05f);
}

static int v07_calls; static float v07_last_x;
static bool v07_wall(float x, float y, float z, void *user) { (void)y; (void)z; v07_calls++; v07_last_x = x; return x > *(const float *)user; }
static void v07_ticks(int n) { for (int i = 0; i < n; i++) arc__fx_tick(1.0f / 60); }
static void v07fx_check(void) {
    /* ---- drawn (software renderer) ---- */
    CHECK(tgt70.id != 0);
    if (tgt70.id) {
        CHECK(near8(t70fxpx(8, 20), 0, 255, 0, 2));         /* flipbook A: frame 1 */
        CHECK(near8(t70fxpx(5, 17), 0, 255, 0, 2) && near8(t70fxpx(11, 23), 0, 255, 0, 2));   /* the whole quad is that one frame */
        CHECK(near8(t70fxpx(24, 20), 0, 0, 255, 2));        /* flipbook B: frame 2 */
        CHECK(near8(t70fxpx(21, 17), 0, 0, 255, 2) && near8(t70fxpx(27, 23), 0, 0, 255, 2));
        const uint8_t *s = t70fxpx(40, 20), *h = t70fxpx(56, 20);
        CHECK(abs(s[0] - 64) <= 3 && s[0] == s[1]);       /* soft: 0.1 / 0.4 of white over black */
        CHECK(near8(h, 255, 255, 255, 1));                /* hard */
        CHECK(near8(t70fxpx(74, 20), 255, 0, 0, 2));        /* decal: red on the grey wall, no z-fighting */
        CHECK(near8(t70fxpx(72, 18), 255, 0, 0, 2) && near8(t70fxpx(77, 22), 255, 0, 0, 2));
        CHECK(near8(t70fxpx(90, 27), 128, 128, 128, 2));    /* the green one was replaced (pool full: oldest goes) */
        CHECK(near8(t70fxpx(66, 29), 128, 128, 128, 2));    /* wall beside the decal */
        const uint8_t *hd = t70fxpx(86, 4), *tl = t70fxpx(16, 4);
        CHECK(hd[1] > 200 && hd[0] < 10);                 /* trail head */
        CHECK(tl[1] > 20 && tl[1] < hd[1] - 60);          /* toward the tail: fading */
        CHECK(t70fxpx(48, 9)[1] < 4 && t70fxpx(48, 0)[1] < 4); /* off the ribbon */
    }
    CHECK(arc_decal_count() == 1);                        /* the 0.05 s decals have expired */
    arc_decal_add((arc_texture){ 0 }, arc_v3_make(-500, 0, 4), arc_v3_make(0, 1, 0), 1, 0, 0xFFFFFF, 0);
    CHECK(arc_decal_count() == 2);                        /* a new one takes an expired slot: the older lasting one stays */
    /* ---- decals: pool, state ---- */
    arc_decals_clear(); CHECK(arc_decal_count() == 0);
    for (int i = 0; i < 300; i++) arc_decal_add((arc_texture){ 0 }, arc_v3_make((float)i, 0, 0), arc_v3_make(0, 1, 0), 1, 0, 0, 0);
    CHECK(arc_decal_count() == ARC_MAX_DECALS);
    arc_decal_add((arc_texture){ 0 }, arc_v3_make(0, 0, 0), arc_v3_make(0, 0, 0), 1, 0, 0, 0);   /* no normal: ignored */
    CHECK(arc_decal_count() == ARC_MAX_DECALS);
    { static uint8_t buf[1 << 16]; arc_writer w = { buf, sizeof buf, 0 }; arc__decal_state(&w);
      CHECK(w.len > 4); arc_decals_clear();
      arc_reader r = { buf, w.len, 0, false }; CHECK(arc__decal_state_read(&r, true) && arc_decal_count() == ARC_MAX_DECALS);
      arc_reader e = { buf, 0, 0, false }; CHECK(arc__decal_state_read(&e, true) && arc_decal_count() == 0); }   /* a state from before them: none */
    /* ---- trails ---- */
    arc_trail t; memset(&t, 0, sizeof t); t.life = 0.1f;
    for (int i = 0; i < 3; i++) arc_trail_push(&t, arc_v3_make((float)i, 0, 0));
    CHECK(t.count == 3);
    for (int i = 0; i < 4; i++) arc_trail_age(&t);
    CHECK(t.count == 2);                                  /* the first point is 6 ticks old: gone at 0.1 s */
    for (int i = 0; i < 10; i++) arc_trail_age(&t);
    CHECK(t.count == 0);
    t.life = 100; for (int i = 0; i < 70; i++) arc_trail_push(&t, arc_v3_make((float)i, 0, 0));
    CHECK(t.count == ARC_TRAIL_MAX && feq(t.p[(t.first + t.count - 1) % ARC_TRAIL_MAX].x, 69) && feq(t.p[t.first].x, 6));
    arc_trail_clear(&t); t.min_dist = 0.5f;
    arc_trail_push(&t, arc_v3_make(0, 0, 0)); arc_trail_push(&t, arc_v3_make(0.1f, 0, 0)); arc_trail_push(&t, arc_v3_make(0.3f, 0, 0));
    CHECK(t.count == 1 && feq(t.p[t.first].x, 0.3f));     /* closer than min_dist: the head moves */
    arc_trail_push(&t, arc_v3_make(1, 0, 0)); CHECK(t.count == 2);
    /* ---- sub-emitters ---- */
    arc_particles_clear();
    static const arc_fx child = { .life = 10, .burst = 3 };
    arc_fx parent = { .life = 0.05f, .on_death = &child, .on_death_count = 5 };
    arc_fx_burst(&parent, arc_v3_make(0, 0, 0), arc_v3_make(0, 1, 0), 2);
    v07_ticks(2); CHECK(arc_particle_count() == 2);
    v07_ticks(2); CHECK(arc_particle_count() == 10);      /* both died at tick 3: five each */
    arc_particles_clear(); parent.on_death_count = 0;
    arc_fx_burst(&parent, arc_v3_make(0, 0, 0), arc_v3_make(0, 1, 0), 2);
    v07_ticks(4); CHECK(arc_particle_count() == 6);       /* count 0: the child's own burst */
    arc_particles_clear();
    static const arc_fx again = { .life = 0.02f, .on_death = &again, .on_death_count = 1 };   /* names itself: a chain */
    arc_fx_burst(&again, arc_v3_make(0, 0, 0), arc_v3_make(0, 1, 0), 1);
    v07_ticks(10); CHECK(arc_particle_count() == 1);
    arc_particles_clear();
    arc_fx drop = { .life = 10, .speed = 5, .gravity = { 0, -9.8f, 0 }, .bounce = 0.1f, .floor_y = 0, .on_bounce = &child, .on_bounce_count = 3 };
    arc_fx_burst(&drop, arc_v3_make(0, 0.5f, 0), arc_v3_make(0, -1, 0), 1);
    v07_ticks(60); CHECK(arc_particle_count() == 4);      /* one hard landing; the small rebounds are too slow to fire */
    /* ---- solid collisions ---- */
    arc_particles_clear();
    static float wall_x = 1;
    arc_fx shot = { .life = 10, .speed = 6, .bounce = 0.5f, .solid = v07_wall, .solid_user = &wall_x };
    arc_fx_burst(&shot, arc_v3_make(0, 0, 0), arc_v3_make(1, 0, 0), 1);
    v07_calls = 0; v07_ticks(40);
    CHECK(arc_particle_count() == 1 && v07_last_x < 0.0f && v07_last_x > -1.0f);   /* came back off the wall at half speed */
    CHECK(v07_calls >= 40 && v07_calls <= 46);            /* one test a tick, three more on the hit */
    arc_particles_clear();
    arc_fx stick = { .life = 10, .speed = 6, .solid = v07_wall, .solid_user = &wall_x, .on_death = &child, .on_death_count = 2 };
    arc_fx_burst(&stick, arc_v3_make(0, 0, 0), arc_v3_make(1, 0, 0), 1);
    v07_ticks(20); CHECK(arc_particle_count() == 2);      /* bounce 0: it ended at the wall (and fired on_death) */
    arc_particles_clear(); arc_decals_clear();
}
