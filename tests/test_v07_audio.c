/* ARC 0.7 audio self-tests (included from test_core.c): variation, voice limits and priorities, ducking, occlusion,
   music stems, beat-synced transitions and stingers. All drive the mixer directly and check rendered samples. */
void arc__audio_init(int rate); void arc__audio_mix(float *out, int frames); void arc__audio_tick(void);
static float v07_occ_amount;
static float v07_occ_hook(arc_v3 from, arc_v3 to, void *user) { (void)from; (void)to; return *(float *)user; }
static int v07_len(const float *out, int n) { int k = 0; for (int i = 0; i < n; i++) if (out[i * 2] != 0) k = i + 1; return k; }

static void v07_variation_limits(void) {
    static int16_t dc[1000]; for (int i = 0; i < 1000; i++) dc[i] = 16384;
    static float out[2 * 4096];
    /* variation: different pitches (lengths) and volumes each play, the same again with the same seed */
    arc_sound s = arc_sound_from_pcm(dc, 1000, 44100);
    arc_voice v0 = arc_play(s, 1, 0); arc__audio_mix(out, 2000); CHECK(v07_len(out, 2000) == 1000 && !arc_voice_playing(v0));   /* no variation: exact */
    float full = out[2 * 5];
    arc_sound_set_variation(s, 5, 0.5f);
    int len[2][6]; float lvl[2][6];
    for (int pass = 0; pass < 2; pass++) {
        arc_audio_seed(7);
        for (int k = 0; k < 6; k++) { arc_play(s, 1, 0); arc__audio_mix(out, 2000); len[pass][k] = v07_len(out, 2000); lvl[pass][k] = out[2 * 5]; }
    }
    int distinct = 0, inrange = 0, same = 0;
    for (int k = 0; k < 6; k++) {
        bool seen = false; for (int j = 0; j < k; j++) seen |= len[0][j] == len[0][k];
        distinct += !seen;
        inrange += len[0][k] >= 748 && len[0][k] <= 1336 && lvl[0][k] >= full * 0.5f - 1e-6f && lvl[0][k] <= full + 1e-6f;
        same += len[0][k] == len[1][k] && lvl[0][k] == lvl[1][k];
    }
    CHECK(distinct >= 5 && inrange == 6 && same == 6);    /* 5 semitones either way, 50..100% volume; reproducible */
    bool lvls_differ = false; for (int k = 1; k < 6; k++) lvls_differ |= lvl[0][k] != lvl[0][0];
    CHECK(lvls_differ);
    arc_audio_seed(8); arc_play(s, 1, 0); arc__audio_mix(out, 2000); CHECK(v07_len(out, 2000) != len[0][0] || out[10] != lvl[0][0]);   /* another seed */
    float r = arc_audio_random(2, 3); CHECK(r >= 2 && r < 3);

    /* limits: a third play over a limit of 2 is refused, or takes the oldest / quietest */
    static int16_t lp[4096]; for (int i = 0; i < 4096; i++) lp[i] = 8192;
    arc_sound l = arc_sound_from_pcm(lp, 4096, 44100);
    arc_sound_set_limit(l, 2, ARC_STEAL_NONE);
    arc_voice a = arc_play_ex(l, 1, 0, 1, true), b = arc_play_ex(l, 1, 0, 1, true), c = arc_play_ex(l, 1, 0, 1, true);
    arc__audio_mix(out, 64); float one = 8192 / 32768.0f;
    CHECK(c.id == 0 && arc_voice_playing(a) && arc_voice_playing(b) && fabsf(out[40] - 2 * one) < 1e-5f);
    arc_sound_set_limit(l, 2, ARC_STEAL_OLDEST); c = arc_play_ex(l, 0.5f, 0, 1, true); arc__audio_mix(out, 64);
    CHECK(c.id != 0 && !arc_voice_playing(a) && arc_voice_playing(b) && arc_voice_playing(c) && fabsf(out[40] - 1.5f * one) < 1e-5f);
    arc_sound_set_limit(l, 2, ARC_STEAL_QUIETEST); arc_voice d = arc_play_ex(l, 0.25f, 0, 1, true); arc__audio_mix(out, 64);
    CHECK(d.id != 0 && arc_voice_playing(b) && !arc_voice_playing(c) && fabsf(out[40] - 1.25f * one) < 1e-5f);   /* c (0.5) was quieter than b (1) */
    arc_voice_stop(b); arc_voice_stop(d); arc_sound_set_limit(l, 0, ARC_STEAL_OLDEST);
    arc_bus_set_limit(ARC_BUS_UI, 1, ARC_STEAL_NONE);
    a = arc_play_bus(l, ARC_BUS_UI, 1, 0); b = arc_play_bus(l, ARC_BUS_UI, 1, 0); c = arc_play(l, 1, 0);
    CHECK(a.id != 0 && b.id == 0 && c.id != 0);           /* the bus is full; SFX is not */
    arc_bus_set_limit(ARC_BUS_UI, 1, ARC_STEAL_OLDEST); b = arc_play_bus(l, ARC_BUS_UI, 1, 0);
    CHECK(b.id != 0 && !arc_voice_playing(a) && arc_voice_playing(c));
    arc_voice_stop(b); arc_voice_stop(c); arc_bus_set_limit(ARC_BUS_UI, 0, ARC_STEAL_OLDEST);
    /* priority: 32 important loops; a lesser sound is refused, a more important one takes a voice */
    arc_sound hi = arc_sound_from_pcm(lp, 4096, 44100), lo = arc_sound_from_pcm(lp, 4096, 44100), top = arc_sound_from_pcm(lp, 4096, 44100);
    arc_sound_set_priority(hi, 1); arc_sound_set_priority(top, 2);
    arc_voice all[32]; int ok = 0; for (int i = 0; i < 32; i++) { all[i] = arc_play_ex(hi, 0.01f, 0, 1, true); ok += all[i].id != 0; }
    CHECK(ok == 32 && arc_play(lo, 1, 0).id == 0);
    arc_voice t = arc_play(top, 1, 0); int still = 0; for (int i = 0; i < 32; i++) still += arc_voice_playing(all[i]);
    CHECK(t.id != 0 && still == 31 && !arc_voice_playing(all[0]));   /* the oldest of the lower priority */
    for (int i = 0; i < 32; i++) arc_voice_stop(all[i]);
    arc_voice_stop(t); arc__audio_mix(out, 64);
}

static void v07_duck_occlusion(void) {
    static float out[2 * 8192];
    static int16_t dc[44100], zero[4410]; for (int i = 0; i < 44100; i++) dc[i] = 16384;
    arc_sound bed = arc_sound_from_pcm(dc, 44100, 44100), talk = arc_sound_from_pcm(zero, 4410, 44100);   /* talk: silent, 0.1 s */
    arc_sound_set_bus(bed, ARC_BUS_MUSIC); arc_sound_set_bus(talk, ARC_BUS_VOICE);
    arc_voice v = arc_play_ex(bed, 1, 0, 1, true); arc__audio_mix(out, 64); float l0 = out[2 * 30];
    arc_bus_duck(ARC_BUS_MUSIC, 1u << ARC_BUS_VOICE, 6, 0.01f, 0.02f);   /* 6 dB in 441 frames, back in 882 */
    arc__audio_mix(out, 64); CHECK(out[2 * 30] == l0);                      /* nothing on the trigger bus: untouched */
    arc_play(talk, 1, 0); arc__audio_mix(out, 600);
    float want = l0 * powf(10, -6 / 20.0f);
    CHECK(out[2 * 220] < l0 * 0.9f && out[2 * 220] > want * 1.05f);         /* half way down */
    CHECK(fabsf(out[2 * 500] - want) < 1e-5f && fabsf(arc_bus_ducked(ARC_BUS_MUSIC) - 6) < 1e-4f);   /* 6 dB down after the attack */
    float step = 0; for (int i = 1; i < 600; i++) step = arc_maxf(step, fabsf(out[i * 2] - out[i * 2 - 2]));
    CHECK(step < 0.002f);                                                   /* a glide, not a jump */
    arc__audio_mix(out, 4000); arc__audio_mix(out, 1000);                    /* talk ends after 4410: release */
    CHECK(out[2 * 999] == l0 && arc_bus_ducked(ARC_BUS_MUSIC) == 0);
    arc_play(talk, 1, 0); arc__audio_mix(out, 600); arc_voice_stop(v);
    arc_music m = arc_music_from_memory(test_qoa, sizeof test_qoa); int ch, rate, frames; int16_t *pcm = arc__qoa_decode_all(test_qoa, sizeof test_qoa, &ch, &rate, &frames);
    arc_music_play(m, true); arc__audio_mix(out, 290);                      /* music on the bus ducks too */
    int ducked = 0; for (int i = 0; i < 290; i++) ducked += fabsf(out[i * 2] - pcm[i] / 32768.0f * want / l0) < 1e-5f;
    CHECK(ducked == 290);
    arc_music_stop(m); free(pcm); arc_bus_duck(ARC_BUS_MUSIC, 0, 0, 0, 0); arc__audio_mix(out, 4096); arc__audio_mix(out, 64);
    CHECK(arc_bus_ducked(ARC_BUS_MUSIC) == 0);

    /* occlusion: quieter and duller; no change at 0; a glide when it changes */
    static int16_t noise[4096]; uint32_t x = 99; for (int i = 0; i < 4096; i++) { x = x * 1664525u + 1013904223u; noise[i] = (int16_t)((int)(x >> 16) - 32768) / 2; }
    arc_sound ns = arc_sound_from_pcm(noise, 4096, 44100);   /* each play ends within its mix */
    arc_set_listener(arc_v3_make(0, 0, 0), arc_v3_make(0, 0, -1), arc_v3_make(0, 1, 0));
    static float clear[2 * 4096]; double e[2] = { 0, 0 }, hf[2] = { 0, 0 };
    arc_play_3d(ns, arc_v3_make(0, 0, -2), 1); arc__audio_mix(clear, 4096);
    arc_set_occlusion(v07_occ_hook, &v07_occ_amount); v07_occ_amount = 0;
    arc_play_3d(ns, arc_v3_make(0, 0, -2), 1); arc__audio_mix(out, 4096);
    CHECK(!memcmp(out, clear, sizeof clear));                               /* hook says 0: bit for bit the same */
    v07_occ_amount = 1; arc_play_3d(ns, arc_v3_make(0, 0, -2), 1); arc__audio_mix(out, 4096);
    for (int i = 1; i < 4096; i++) {
        e[0] += clear[i * 2] * clear[i * 2]; hf[0] += (clear[i * 2] - clear[i * 2 - 2]) * (clear[i * 2] - clear[i * 2 - 2]);
        e[1] += out[i * 2] * out[i * 2]; hf[1] += (out[i * 2] - out[i * 2 - 2]) * (out[i * 2] - out[i * 2 - 2]);
    }
    CHECK(e[1] < e[0] * 0.16 && e[1] > e[0] * 0.001);                       /* quieter (gain 0.4 and the filter) */
    CHECK(hf[1] / e[1] < 0.2 * hf[0] / e[0]);                               /* duller: far less high-frequency energy */
    v07_occ_amount = 0; arc_sound dcs = arc_sound_from_pcm(dc, 44100, 44100);
    arc_voice w = arc_play_3d_ex(dcs, arc_v3_make(0, 0, -1), 1, 1, true); arc__audio_mix(out, 64); float open = out[2 * 50];
    v07_occ_amount = 1; arc__audio_tick(); arc__audio_mix(out, 4096);       /* a wall moves in */
    step = 0; for (int i = 1; i < 4096; i++) step = arc_maxf(step, fabsf(out[i * 2] - out[i * 2 - 2]));
    CHECK(step < 0.001f && fabsf(out[2 * 600] - open) > 0.02f && fabsf(out[2 * 3000] - open * 0.4f) < 1e-3f);   /* glides to 0.4 in 50 ms */
    v07_occ_amount = 0; arc__audio_tick(); arc__audio_mix(out, 4096); CHECK(fabsf(out[2 * 4000] - open) < 1e-5f);
    arc_voice_stop(w); arc_set_occlusion(NULL, NULL);
    arc_voice u = arc_play_ex(dcs, 1, 0, 1, true); arc__audio_mix(out, 64); open = out[2 * 50];
    arc_voice_set_occlusion(u, 0.5f); arc__audio_mix(out, 4096);            /* by hand on a plain voice: gain 1 - 0.5 * 0.6 */
    CHECK(fabsf(out[2 * 4000] - open * 0.7f) < 1e-3f);
    arc_voice_stop(u); arc__audio_mix(out, 64);
}

static void v07_music(void) {
    static float out[2 * 4096];
    int ch, rate, frames; int16_t *pcm = arc__qoa_decode_all(test_qoa, sizeof test_qoa, &ch, &rate, &frames);
    float k = 1;   /* the master volume (three stems at once would clip) */
    #define V07_P(i) (pcm[(i) % 300] / 32768.0f * k)
    /* stems: sample-locked to the lead, even when one has played on its own before */
    arc_music lead = arc_music_from_memory(test_qoa, sizeof test_qoa), s1 = arc_music_from_memory(test_qoa, sizeof test_qoa), s2 = arc_music_from_memory(test_qoa, sizeof test_qoa);
    arc_music_play(s1, true); arc__audio_mix(out, 77); arc_music_stop(s1);
    CHECK(arc_music_link(s1, lead) && arc_music_link(s2, lead));
    arc_music_set_volume(s1, 0.5f); arc_music_set_volume(s2, 0.25f);
    arc_music_play(lead, true);
    int aligned = 0, at = 0;
    for (int n = 37; at < 1500; n = n * 3 % 101 + 5) { arc__audio_mix(out, n); for (int i = 0; i < n; i++) aligned += fabsf(out[i * 2] - V07_P(at + i) * 1.75f) < 2e-5f; at += n; }
    CHECK(aligned == at && arc_music_playing(s1));                          /* across loops and odd chunk sizes */
    arc_music_pause(lead, true); arc__audio_mix(out, 50); CHECK(out[2 * 20] == 0 && !arc_music_playing(s2));
    arc_music_pause(lead, false); arc__audio_mix(out, 100);
    aligned = 0; for (int i = 0; i < 100; i++) aligned += fabsf(out[i * 2] - V07_P(at + i) * 1.75f) < 2e-5f;
    CHECK(aligned == 100); at += 100;
    /* intensity: the lead always, stem 1 over 0..0.5, stem 2 over 0.5..1 */
    arc_music_set_volume(s1, 1); arc_music_set_volume(s2, 0.5f); k = 0.5f; arc_set_master_volume(k);
    arc_music_set_intensity(lead, 0, 0, ARC_SYNC_NOW); arc__audio_mix(out, 50);
    CHECK(fabsf(out[2 * 10] - V07_P(at + 10)) < 2e-5f); at += 50;
    arc_music_set_intensity(lead, 0.5f, 0, ARC_SYNC_NOW); arc__audio_mix(out, 50);
    CHECK(fabsf(out[2 * 10] - V07_P(at + 10) * 2) < 2e-5f); at += 50;
    arc_music_set_intensity(lead, 0.75f, 0, ARC_SYNC_NOW); arc__audio_mix(out, 50);
    CHECK(fabsf(out[2 * 10] - V07_P(at + 10) * 2.25f) < 2e-5f); at += 50;
    arc_music_set_intensity(lead, 1, 200.0f / 44100, ARC_SYNC_NOW); arc__audio_mix(out, 300);   /* stem 2: 0.5 -> 1 in 200 frames */
    CHECK(fabsf(out[2 * 99] - V07_P(at + 99) * 2.375f) < 1e-4f && fabsf(out[2 * 250] - V07_P(at + 250) * 2.5f) < 2e-5f); at += 300;
    arc_music_fade(lead, 0, 0); arc__audio_mix(out, 50); CHECK(fabsf(out[2 * 10] - V07_P(at + 10) * 1.5f) < 2e-5f); at += 50;
    arc_music_fade(lead, 1, 0);
    /* intensity on the next bar: tempo 26460 bpm = 100 frames a beat, 3 beats = 300 frames a bar (the loop) */
    arc_music_set_tempo(lead, 26460, 3, 0);
    arc_music_set_intensity(lead, 0, 0, ARC_SYNC_BAR); int bar = (300 - at % 300) % 300;
    arc__audio_mix(out, 400); int before = 0, after = 0;
    for (int i = 0; i < 400; i++) { if (i < bar) before += fabsf(out[i * 2] - V07_P(at + i) * 2.5f) < 2e-5f; else after += fabsf(out[i * 2] - V07_P(at + i)) < 2e-5f; }
    CHECK(before == bar && after == 400 - bar); at += 400;
    CHECK(fabsf(arc_music_position(lead) - (float)(at % 300) / 44100) < 1e-6f && fabsf(arc_music_beat(lead) - (float)(at % 300) / 100) < 1e-3f);
    k = 1; arc_set_master_volume(k); arc_music_stop(lead); arc__audio_mix(out, 10); CHECK(out[2 * 5] == 0 && !arc_music_playing(s1));
    arc_music_link(s1, (arc_music){ 0 }); arc_music_link(s2, (arc_music){ 0 });

    /* transitions on the next beat / bar */
    arc_music A = arc_music_from_memory(test_qoa, sizeof test_qoa), B = arc_music_from_memory(test_qoa, sizeof test_qoa);
    arc_music_set_tempo(A, 26460, 3, 0); arc_music_set_tempo(B, 26460, 3, 0.5f / 441);   /* B: first beat 50 frames in */
    arc_music_play(A, true); arc__audio_mix(out, 130);
    arc_music_switch(A, B, true, ARC_SYNC_BEAT, 0); arc__audio_mix(out, 300);   /* frames 130..429; the beat at 200 */
    int good = 0; for (int i = 0; i < 300; i++) { int f = 130 + i; good += fabsf(out[i * 2] - (f < 200 ? V07_P(f) : V07_P(f - 200))) < 2e-5f; }
    CHECK(good == 300 && !arc_music_playing(A) && arc_music_playing(B));
    /* B is 230 frames in; its bars start at 50, 350 (= loop + 50): the next is 120 frames away; crossfade 100 frames */
    arc_music_switch(B, A, false, ARC_SYNC_BAR, 100.0f / 44100); arc__audio_mix(out, 400);
    good = 0; for (int i = 0; i < 120; i++) good += fabsf(out[i * 2] - V07_P(230 + i)) < 2e-5f;
    int xf = 0; for (int i = 120; i < 219; i++) { float g = (float)(i - 119) * 0.01f; xf += fabsf(out[i * 2] - V07_P(230 + i) * (1 - g) - V07_P(i - 120) * g) < 1e-4f; }
    CHECK(good == 120 && xf == 99);                                         /* B fades out as A fades in, 100 frames */
    good = 0; for (int i = 220; i < 300; i++) good += fabsf(out[i * 2] - V07_P(i - 120)) < 2e-5f;
    CHECK(good == 80 && !arc_music_playing(B) && arc_music_playing(A));     /* faded out and stopped: only A */
    /* A has played 280 of its 300 (no loop); a stinger on the next beat is at 300, i.e. 20 frames on */
    static int16_t dc[500]; for (int i = 0; i < 500; i++) dc[i] = 10000;
    arc_music_switch((arc_music){ 0 }, A, true, ARC_SYNC_NOW, 0); arc_music_set_volume(A, 0); arc__audio_mix(out, 130);   /* A again from the top, silent */
    arc_sound st = arc_sound_from_pcm(dc, 500, 44100);
    arc_voice sv = arc_music_stinger(A, st, 1, ARC_SYNC_BEAT); arc__audio_mix(out, 300);
    int first = -1; for (int i = 0; i < 300; i++) if (out[i * 2] != 0) { first = i; break; }
    CHECK(sv.id != 0 && first == 70 && fabsf(out[2 * 71] - 10000 / 32768.0f) < 1e-5f);   /* exactly on beat 2 (frame 200) */
    arc_voice_stop(sv); arc_set_bus_volume(ARC_BUS_MUSIC, 0.5f); sv = arc_music_stinger(A, st, 1, ARC_SYNC_BAR); arc__audio_mix(out, 300);   /* A at 130 + 300 = 430 = 130 into a loop */
    first = -1; for (int i = 0; i < 300; i++) if (out[i * 2] != 0) { first = i; break; }
    CHECK(first == 170 && fabsf(out[2 * 171] - 0.5f * 10000 / 32768.0f) < 1e-5f);   /* the next bar, on the music bus */
    arc_set_bus_volume(ARC_BUS_MUSIC, 1); arc_music_stop(A); arc_voice_stop(sv);
    arc_music none = { 0 }; arc_voice nv = arc_music_stinger(none, st, 1, ARC_SYNC_BAR); arc__audio_mix(out, 10);
    CHECK(nv.id != 0 && out[0] != 0);                                       /* no clock: at once */
    arc_voice_stop(nv); arc__audio_mix(out, 10);
    #undef V07_P
    free(pcm);
}

static void v07_audio(void) {
    extern bool arc__audio_test_mode; arc__audio_test_mode = true;
    arc__audio_init(44100); arc_set_master_volume(1);
    v07_variation_limits(); v07_duck_occlusion(); v07_music();
    for (int b = 0; b < ARC_BUS_COUNT; b++) arc_set_bus_volume((arc_bus)b, 1);
    arc_set_master_volume(0.8f); arc__audio_test_mode = false;
}
