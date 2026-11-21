/* ARC 0.10.13 self-test: music loop sections (QOA and MP3: after the first pass the samples are the section's, and the
   position stays inside it), the crossfade, and the 2D vertex count in the stats. Included by test_core.c (after
   test_v1012.c); v1013_init() runs in init. Uses test_qoa / test_mp3 from test_core.c. Names start with v1013_. */
static void v1013_music(void) {
    extern bool arc__audio_test_mode; arc__audio_test_mode = true;
    /* QOA, 300 samples at 44.1 kHz (the mixer's rate: one sample a frame); loop 100..250 */
    int ch = 0, rate = 0, frames = 0; int16_t *pcm = arc__qoa_decode_all(test_qoa, sizeof test_qoa, &ch, &rate, &frames);
    arc__audio_init(44100); arc_set_master_volume(1.0f);
    arc_music m = arc_music_from_memory(test_qoa, sizeof test_qoa);
    arc_music_set_loop(m, 100.0f / 44100.0f, 250.0f / 44100.0f); arc_music_play(m, true);
    static float out[2 * 1200]; arc__audio_mix(out, 1000);
    int good = 0;   /* first pass 0..249, then 100..249 again and again */
    for (int i = 0; i < 1000; i++) { int want = i < 250 ? i : 100 + (i - 250) % 150; good += fabsf(out[i * 2] - pcm[want] / 32768.0f) < 1e-4f; }
    float pos = arc_music_position(m);
    CHECK(good == 1000 && arc_music_playing(m) && pos >= 100.0f / 44100.0f && pos < 250.0f / 44100.0f);
    /* the crossfade: no step at the seam (the end fades out while the start fades in) */
    arc_music_stop(m); arc_music_set_loop_fade(m, 20.0f / 44100.0f); arc_music_play(m, true); arc__audio_mix(out, 600);
    float jump = 0; for (int i = 1; i < 600; i++) jump = arc_maxf(jump, fabsf(out[i * 2] - out[i * 2 - 2]));
    CHECK(jump < 0.2f && arc_music_playing(m));
    arc_music_stop(m); arc_music_set_loop_fade(m, 0); arc_music_set_loop(m, 0, 0);   /* whole file again */
    arc_music_play(m, true); arc__audio_mix(out, 350); CHECK(fabsf(out[300 * 2] - pcm[0] / 32768.0f) < 1e-4f);
    arc_music_stop(m); free(pcm);
    /* MP3, 2000 samples at 22.05 kHz; loop 800..1600 (the section's start is mid-frame: decoded to it once) */
    arc__audio_init(22050);
    arc_music a = arc_music_from_memory(test_mp3, sizeof test_mp3), b = arc_music_from_memory(test_mp3, sizeof test_mp3);
    arc_music_play(b, false); static float ref[2 * 2100]; arc__audio_mix(ref, 2100); arc_music_stop(b);   /* the plain decode */
    arc_music_set_loop(a, 800.0f / 22050.0f, 1600.0f / 22050.0f); arc_music_play(a, true); arc__audio_mix(out, 1200);
    static float out2[2 * 1000]; arc__audio_mix(out2, 1000);   /* frames 1200..2199: 1200..1599, then 800..1399 */
    int same = 0;
    for (int i = 0; i < 1000; i++) { int f = 1200 + i, want = f < 1600 ? f : 800 + (f - 1600) % 800; same += fabsf(out2[i * 2] - ref[want * 2]) < 1e-4f; }
    CHECK(same >= 990);
    float pa = arc_music_position(a); CHECK(pa >= 800.0f / 22050.0f && pa < 1600.0f / 22050.0f);
    arc_music_stop(a);
    arc__audio_init(44100); arc__audio_test_mode = false;
}
static void v1013_stats(void) {
    arc_gfx_stats st = arc_stats();
    CHECK(st.vertices_2d_max >= (1 << 20) && st.vertices_2d >= 0 && st.vertices_2d <= st.vertices_2d_max);
    CHECK(arc_offset2d().x == 0 && arc_offset2d().y == 0);
}
static void v1013_terrain(void) {   /* set_splat feeds arc_terrain_layer_weight and later paints; colour maps come and go */
    uint8_t px[4 * 4 * 4]; memset(px, 200, sizeof px);
    arc_texture l0 = arc_texture_create(4, 4, px, false), l1 = arc_texture_create(4, 4, px, false);
    arc_terrain t = arc_terrain_create(&(arc_terrain_desc){ .samples_x = 17, .samples_z = 17, .size_x = 16, .size_z = 16, .splat_res = 16,
                                                            .layers = { l0, l1 }, .layer_count = 2 });
    arc_texture_destroy(l0); arc_texture_destroy(l1);   /* copied in */
    CHECK(t.id && arc_terrain_splat_res(t) == 16);
    static uint8_t w[16 * 16 * 4];
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) { uint8_t *q = &w[(y * 16 + x) * 4]; q[0] = x < 8 ? 255 : 0; q[1] = x < 8 ? 0 : 255; q[2] = q[3] = 0; }
    arc_terrain_set_splat(t, w);
    CHECK(arc_terrain_layer_weight(t, 2.0f, 8.0f, 1) < 0.01f && arc_terrain_layer_weight(t, 14.0f, 8.0f, 1) > 0.99f);
    arc_terrain_paint(t, 2.0f, 2.0f, 3.0f, 1, 1.0f);   /* a paint elsewhere leaves the right half as set */
    CHECK(arc_terrain_layer_weight(t, 14.0f, 14.0f, 1) > 0.99f && arc_terrain_layer_weight(t, 2.0f, 2.0f, 1) > 0.3f);
    uint8_t red[4 * 4 * 4]; for (int i = 0; i < 16; i++) { red[i * 4] = 255; red[i * 4 + 1] = red[i * 4 + 2] = 0; red[i * 4 + 3] = 255; }
    arc_texture c = arc_texture_create(4, 4, red, false);
    arc_terrain_set_color_map(t, c); arc_texture_destroy(c);   /* copied */
    arc_terrain_set_color_map(t, (arc_texture){ 0 });
    arc_terrain_destroy(t);
}
static void v1013_init(void) { v1013_music(); v1013_stats(); v1013_terrain(); }
