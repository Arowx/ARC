/* ARC audio: a tiny synth, sound files (.mp3 / .qoa / .wav), streamed music, and a 32-voice mixer.
 * Platforms call arc__audio_mix() from their audio callback. Game code talks to the mixer through a
 * single-producer/single-consumer command ring, so no locks are needed. Sounds keep their own sample
 * rate and channel count; the mixer resamples while it plays, so any device rate works.
 *
 * QOA ("Quite OK Audio", https://qoaformat.org, MIT/public format) is decoded here; tools/audio_convert.py
 * turns MP3/OGG/WAV into .qoa. About 3.2 bits per sample, decoded with a handful of integer ops. */
#include "arc_internal.h"

#define MAX_SOUNDS 1024
#define MAX_VOICES 32
#define MAX_MUSIC  32
#define RING 1024

/* ---- QOA decoding ------------------------------------------------------- */
#define QOA_SLICE 20
#define QOA_FRAME_SLICES 256
#define QOA_FRAME_LEN (QOA_SLICE * QOA_FRAME_SLICES)
typedef struct { int history[4], weights[4]; } qoa_lms;
static int qoa_dequant[16][8];
static void qoa_tables(void) {
    static bool done; if (done) return; done = true;
    static const float base[8] = { 0.75f, -0.75f, 2.5f, -2.5f, 4.5f, -4.5f, 7.0f, -7.0f };
    for (int s = 0; s < 16; s++) {
        float sf = roundf(powf((float)(s + 1), 2.75f));
        for (int q = 0; q < 8; q++) { float v = sf * base[q]; qoa_dequant[s][q] = (int)(v < 0 ? v - 0.5f : v + 0.5f); }
    }
}
static uint64_t be64(const uint8_t *p) { uint64_t v = 0; for (int i = 0; i < 8; i++) v = (v << 8) | p[i]; return v; }
static int clamp16(int v) { return v < -32768 ? -32768 : v > 32767 ? 32767 : v; }
typedef struct { int channels, rate, frames; } qoa_info;   /* frames = samples per channel */
static bool qoa_header(const uint8_t *d, size_t n, qoa_info *qi) {
    if (n < 16 || memcmp(d, "qoaf", 4)) return false;
    uint32_t total = (uint32_t)d[4] << 24 | (uint32_t)d[5] << 16 | (uint32_t)d[6] << 8 | d[7];
    uint64_t fh = be64(d + 8);
    qi->channels = (int)((fh >> 56) & 0xff); qi->rate = (int)((fh >> 32) & 0xffffff); qi->frames = (int)total;
    return qi->channels >= 1 && qi->channels <= 8 && qi->rate > 0 && total > 0;
}
/* decode one frame at d (n bytes left) into out (interleaved); returns bytes used, 0 on error */
static size_t qoa_frame(const uint8_t *d, size_t n, int channels, int16_t *out, int *samples_out) {
    if (n < 8) return 0;
    uint64_t fh = be64(d);
    int ch = (int)((fh >> 56) & 0xff), samples = (int)((fh >> 16) & 0xffff); size_t fsize = (size_t)(fh & 0xffff);
    if (ch != channels || fsize > n || samples > QOA_FRAME_LEN) return 0;
    size_t need = 8 + (size_t)ch * 16 + (size_t)((samples + QOA_SLICE - 1) / QOA_SLICE) * ch * 8;
    if (fsize < need) return 0;
    const uint8_t *p = d + 8;
    qoa_lms lms[8];
    for (int c = 0; c < ch; c++) {
        uint64_t h = be64(p), w = be64(p + 8); p += 16;
        for (int i = 0; i < 4; i++) { lms[c].history[i] = (int16_t)(h >> 48); h <<= 16; lms[c].weights[i] = (int16_t)(w >> 48); w <<= 16; }
    }
    for (int si = 0; si < samples; si += QOA_SLICE)
        for (int c = 0; c < ch; c++) {
            uint64_t slice = be64(p); p += 8;
            int sf = (int)((slice >> 60) & 0xf); slice <<= 4;
            int end = si + QOA_SLICE < samples ? si + QOA_SLICE : samples;
            qoa_lms *L = &lms[c];
            for (int s = si; s < end; s++) {
                int pred = 0; for (int i = 0; i < 4; i++) pred += L->weights[i] * L->history[i];
                pred >>= 13;
                int dq = qoa_dequant[sf][(slice >> 61) & 7]; slice <<= 3;
                int r = clamp16(pred + dq);
                out[s * ch + c] = (int16_t)r;
                int delta = dq >> 4;
                for (int i = 0; i < 4; i++) L->weights[i] += L->history[i] < 0 ? -delta : delta;
                L->history[0] = L->history[1]; L->history[1] = L->history[2]; L->history[2] = L->history[3]; L->history[3] = r;
            }
        }
    *samples_out = samples;
    return fsize;
}

/* ---- WAV (16-bit PCM) ---------------------------------------------------- */
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static const uint8_t *wav_parse(const uint8_t *d, size_t n, int *ch, int *rate, int *frames) {
    if (n < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) return NULL;
    size_t at = 12; int fmt_ok = 0;
    while (at + 8 <= n) {
        uint32_t len = le32(d + at + 4); const uint8_t *body = d + at + 8;
        if (at + 8 + len > n) len = (uint32_t)(n - at - 8);
        if (!memcmp(d + at, "fmt ", 4) && len >= 16) {
            int format = body[0] | body[1] << 8, bits = body[14] | body[15] << 8;
            *ch = body[2] | body[3] << 8; *rate = (int)le32(body + 4);
            fmt_ok = (format == 1 || format == 0xFFFE) && bits == 16 && *ch >= 1 && *ch <= 2;
        } else if (!memcmp(d + at, "data", 4) && fmt_ok) {
            *frames = (int)(len / (2u * (unsigned)*ch)); return body;
        }
        at += 8 + len + (len & 1);
    }
    return NULL;
}

/* ---- mixer state ----------------------------------------------------------- */
typedef struct { int16_t *data; int frames, channels, rate; int bus; int max_voices, steal, priority; float vary_semis, vary_vol; int vframes; } sound_rec;   /* vframes: 0.9.8 --no-assets, the file's length */
typedef struct { int sound; double pos, step; float gl, gr; uint32_t gen; bool loop; float lp_a, lp_l, lp_r, send; int bus;
                 int delay; float occ, occ_t, oz[4]; } voice;   /* mixer thread only; delay: frames before it starts; occ: occlusion now / wanted */
#define MAX_STREAMS 8
typedef struct {
    bool used; int16_t *buf; int cap, channels, rate; volatile int bus;   /* cap in frames */
    volatile unsigned w, r;                                   /* frames written (game) / read (mixer) */
    volatile float volume; volatile int released;             /* released: the mixer has let go, the slot can be reused */
    float s0[2], s1[2]; double t; bool primed;                /* mixer resampler */
} stream_rec;
typedef struct { float cur, target, step; int delay; } music_ramp;   /* a gain gliding to target, step per frame, after delay frames */
typedef struct music_rec_s {
    bool used; const uint8_t *data; size_t size; qoa_info info;
    arc__mp3 *mp3;                                        /* MP3 music: the decoder (else QOA) */
    /* owned by the mixer thread */
    size_t at; int16_t buf[QOA_FRAME_LEN * 2]; int buf_len, buf_pos; float s0[2], s1[2]; double t; bool primed, draining;
    /* shared */
    volatile int playing, looping, paused, ended; volatile float volume;
    /* adaptive music: tempo and stems (game side), start delay, fades and position (mixer side) */
    volatile float bpm, first_beat; volatile int beats_per_bar, lead, layer, pending;   /* pending: waits for its start command */
    int64_t played; int delay; music_ramp fade, xf; bool xf_stop; volatile float pos;
    /* 0.10.13 loop section (Vector Trader): lp_start..lp_end source samples (lp_end 0: the file's end); MP3 jumps by
       copying lp_snap (the decoder's state at the frame holding lp_start) back, QOA to byte lp_at; then lp_skip samples
       are dropped. next_src counts source samples from the file's start; i0 / i1: s0's and s1's (for the position).
       lp_fade > 0: at lp_end - lp_fade a copy of the stream (tail) plays on, fading out, while the main one jumps. */
    volatile int lp_on; int64_t lp_start, lp_end; void *volatile lp_snap; size_t lp_at; int lp_skip, lp_fade;
    int skip_pending; int64_t next_src, i0, i1; struct music_rec_s *tail; int tail_n;
} music_rec;
enum { CMD_PLAY, CMD_MUSIC_PLAY, CMD_MUSIC_STOP, CMD_VOICE_STOP, CMD_VOICE_SET, CMD_STREAM_CLEAR, CMD_STREAM_FREE, CMD_FX,
       CMD_MUSIC_FADE, CMD_MUSIC_INTENSITY, CMD_MUSIC_SWITCH };
typedef struct { int kind, id; float gl, gr; int loop; int slot; uint32_t gen; float pitch, lp, send; int bus; float occ; int when, clock; } cmd;

static sound_rec g_sounds[MAX_SOUNDS]; static int g_sound_count = 1;   /* id 0 = none */
static voice g_voices[MAX_VOICES];
/* voice slots are handed out by the game thread so a handle exists at once; the mixer clears busy when a sound ends */
static volatile uint32_t g_vgen[MAX_VOICES]; static volatile int g_vbusy[MAX_VOICES]; static uint64_t g_vstart[MAX_VOICES], g_vcounter;
static bool g_vloop[MAX_VOICES];
static stream_rec g_streams[MAX_STREAMS];
bool arc__audio_test_mode;   /* the self-test drives the mixer itself, so headless runs may play */
bool arc__audio_silent;
bool arc__audio_rb;          /* 0.8.1: the silence is a rollback re-simulation: a sound heard for the first time may still start */
int  arc__rb_sound_gate(uint32_t snd, arc_voice *prev);   /* arc_rollback.c: -1 no match, 0 play (recorded), 1 already played */
void arc__rb_sound_voice(arc_voice v);
static bool no_mixer(void) { return (arc_headless() && !arc__audio_test_mode) || arc__audio_silent; }
static music_rec g_music[MAX_MUSIC];
static cmd g_ring[RING]; static volatile unsigned g_ring_w, g_ring_r;
static volatile float g_master = 0.8f;
static int g_rate = 44100;

void arc__audio_init(int rate) { if (rate > 0) g_rate = rate; qoa_tables(); }
int  arc__audio_rate(void) { return g_rate; }
void arc_set_master_volume(float v) { g_master = arc_clampf(v, 0, 1); }

static bool push(cmd c) {
    unsigned w = g_ring_w; if (w - g_ring_r >= RING) return false;
    g_ring[w % RING] = c; ARC__BARRIER(); g_ring_w = w + 1; return true;
}
static arc_sound add_sound(int16_t *data, int frames, int channels, int rate) {
    arc_sound none = { 0 };
    if (g_sound_count >= MAX_SOUNDS || frames <= 0 || rate <= 0) { free(data); arc_log("sound not added (limit %d)", MAX_SOUNDS); return none; }
    sound_rec *s = &g_sounds[g_sound_count]; s->data = data; s->frames = frames; s->channels = channels; s->rate = rate;
    arc_sound r = { (uint32_t)g_sound_count++ }; return r;
}

arc_sound arc_sound_from_pcm(const int16_t *mono, int frames, int rate) {
    arc_sound none = { 0 }; if (frames <= 0) return none;
    int16_t *d = (int16_t *)arc__xalloc(sizeof(int16_t) * (size_t)frames); memcpy(d, mono, sizeof(int16_t) * (size_t)frames);
    return add_sound(d, frames, 1, rate);
}
/* any thread (the tables are filled first: arc__decoders_warm): PCM out, or the reason it failed */
const char *arc__sound_decode(const uint8_t *d, size_t n, int16_t **pcm_out, int *frames_out, int *channels_out, int *rate_out) {
    qoa_info qi; int ch = 0, rate = 0, frames = 0; const uint8_t *pcm;
    *pcm_out = NULL; *frames_out = *channels_out = *rate_out = 0;
    if (qoa_header(d, n, &qi)) {
        int16_t *out = (int16_t *)arc__xalloc(sizeof(int16_t) * (size_t)qi.frames * qi.channels + QOA_FRAME_LEN * 8 * 2);
        size_t at = 8; int got = 0;
        while (got < qi.frames && at < n) {
            int fs = 0; size_t used = qoa_frame(d + at, n - at, qi.channels, out + (size_t)got * qi.channels, &fs);
            if (!used) break;
            at += used; got += fs;
        }
        if (got <= 0) { free(out); return "bad .qoa data"; }
        *pcm_out = out; *frames_out = got < qi.frames ? got : qi.frames; *channels_out = qi.channels; *rate_out = qi.rate; return NULL;
    }
    if ((pcm = wav_parse(d, n, &ch, &rate, &frames)) != NULL && frames > 0) {
        int16_t *out = (int16_t *)arc__xalloc(sizeof(int16_t) * (size_t)frames * ch);
        for (size_t i = 0; i < (size_t)frames * ch; i++) out[i] = (int16_t)(pcm[i * 2] | pcm[i * 2 + 1] << 8);
        *pcm_out = out; *frames_out = frames; *channels_out = ch; *rate_out = rate; return NULL;
    }
    arc__mp3 *mp = (arc__mp3 *)arc__xalloc(arc__mp3_state_size());
    if (arc__mp3_open(mp, d, n)) {                        /* MP3: decoded whole now */
        int nch = arc__mp3_channels(mp), cap = 1 << 16, got = 0, k;
        int16_t *out = (int16_t *)arc__xalloc(sizeof(int16_t) * (size_t)cap * nch), frame[1152 * 2];
        while ((k = arc__mp3_read(mp, frame)) >= 0) {
            if (got + k > cap) { cap = cap * 2 + k; int16_t *grown = (int16_t *)realloc(out, sizeof(int16_t) * (size_t)cap * nch); if (!grown) { free(out); free(mp); return "out of memory"; } out = grown; }
            memcpy(out + (size_t)got * nch, frame, sizeof(int16_t) * (size_t)k * nch); got += k;
        }
        int mrate = arc__mp3_rate(mp); free(mp);
        if (got <= 0) { free(out); return "MP3 data has no audio"; }
        *pcm_out = out; *frames_out = got; *channels_out = nch; *rate_out = mrate; return NULL;
    }
    free(mp);
    return "sound data is not .mp3, .qoa or 16-bit .wav";
}
static int snd_stub(int16_t **pcm, int *frames, int ch) {   /* 0.9.8 --no-assets: one silent frame; returns the real length */
    int real = *frames;
    if (!arc__no_assets || real <= 1) return 0;
    free(*pcm); *pcm = (int16_t *)arc__xalloc(sizeof(int16_t) * (size_t)(ch > 0 ? ch : 1)); memset(*pcm, 0, sizeof(int16_t) * (size_t)(ch > 0 ? ch : 1));
    *frames = 1; return real;
}
arc_sound arc_sound_from_memory(const uint8_t *d, size_t n) {
    arc_sound none = { 0 }; qoa_tables();
    int16_t *pcm; int frames, ch, rate;
    const char *err = arc__sound_decode(d, n, &pcm, &frames, &ch, &rate);
    if (err) { arc_log("%s", err); return none; }
    int real = snd_stub(&pcm, &frames, ch);
    arc_sound r = add_sound(pcm, frames, ch, rate);
    if (r.id) g_sounds[r.id].vframes = real;
    return r;
}
/* 0.7.1 the loader: a slot with no samples (plays nothing) until the decoded sound arrives */
uint32_t arc__sound_reserve(void) {
    if (g_sound_count >= MAX_SOUNDS) { arc_log("sound not added (limit %d)", MAX_SOUNDS); return 0; }
    sound_rec *s = &g_sounds[g_sound_count]; s->data = NULL; s->frames = 0; s->channels = 1; s->rate = 44100;
    return (uint32_t)g_sound_count++;
}
void arc__sound_finish(uint32_t id, int16_t *pcm, int frames, int channels, int rate) {
    if (!id || (int)id >= g_sound_count || g_sounds[id].data || !pcm || frames <= 0 || rate <= 0) { free(pcm); return; }
    int real = snd_stub(&pcm, &frames, channels);
    sound_rec *s = &g_sounds[id]; s->frames = frames; s->channels = channels; s->rate = rate; s->vframes = real;
    ARC__BARRIER(); s->data = pcm;             /* the mixer only looks at sounds that have data */
}
void arc__decoders_warm(void) { jpg_idct_init(); mp3_init_tables(); qoa_tables(); }   /* lazy tables, filled once */
bool arc_sound_ready(arc_sound s) { return s.id && (int)s.id < g_sound_count && g_sounds[s.id].data != NULL; }
arc_sound arc_sound_load(const char *path) {
    if (arc__load_queueing()) { arc_sound q = { arc__load_queue(ARC__LOAD_SOUND, path, 0) }; return q; }
    size_t mark = arc_arena_mark(arc_frame()), n = 0;
    uint8_t *d = arc_file_read(path, arc_frame(), &n);
    arc_sound s = { 0 };
    if (d) s = arc_sound_from_memory(d, n); else arc_log("cannot load sound %s", path);
    arc_arena_pop(arc_frame(), mark);
    if (s.id) arc__hot_watch(ARC__HOT_SOUND, s.id, path);
    return s;
}
float arc_sound_length(arc_sound s) {
    if (!s.id || (int)s.id >= g_sound_count) return 0;
    return (float)(g_sounds[s.id].vframes ? g_sounds[s.id].vframes : g_sounds[s.id].frames) / (float)g_sounds[s.id].rate;
}

arc_sound arc_sound_make(const arc_sfx *s) {
    const int rate = 44100;
    float total = s->attack + s->sustain + s->decay; if (total <= 0) total = 0.1f;
    int frames = (int)(total * rate);
    int16_t *d = (int16_t *)arc__xalloc(sizeof(int16_t) * (size_t)(frames + 1));
    float phase = 0; uint32_t noise = 0x12345678u; float noise_v = 0;
    float duty = s->duty > 0 ? s->duty : 0.5f, vol = s->volume > 0 ? s->volume : 0.5f;
    for (int i = 0; i < frames; i++) {
        float t = (float)i / rate, k = t / total;
        float freq = s->freq + (s->freq_end - s->freq) * k;
        if (s->vibrato_hz > 0 && s->vibrato_depth != 0) freq *= powf(2.0f, s->vibrato_depth / 12.0f * sinf(ARC_TAU * s->vibrato_hz * t));
        if (freq < 1) freq = 1;
        float prev = phase;
        phase += freq / rate; if (phase >= 1) phase -= floorf(phase);
        float v;
        switch (s->wave) {
        case ARC_WAVE_SAW: v = phase * 2 - 1; break;
        case ARC_WAVE_SINE: v = sinf(phase * ARC_TAU); break;
        case ARC_WAVE_TRIANGLE: v = phase < 0.5f ? phase * 4 - 1 : 3 - phase * 4; break;
        case ARC_WAVE_NOISE: if (phase < prev) { noise ^= noise << 13; noise ^= noise >> 17; noise ^= noise << 5; noise_v = (noise & 0xFFFF) / 32768.0f - 1; } v = noise_v; break;
        default: v = phase < duty ? 1.0f : -1.0f; break;
        }
        float env = t < s->attack ? t / s->attack : t < s->attack + s->sustain ? 1.0f : 1.0f - (t - s->attack - s->sustain) / (s->decay > 0 ? s->decay : 1e-3f);
        d[i] = (int16_t)(arc_clampf(v * arc_clampf(env, 0, 1) * vol, -1, 1) * 32767.0f);
    }
    return add_sound(d, frames, 1, rate);
}

static void pan_gains(float volume, float pan, float *gl, float *gr) {
    pan = arc_clampf(pan, -1, 1); *gl = volume * (pan <= 0 ? 1 : 1 - pan); *gr = volume * (pan >= 0 ? 1 : 1 + pan);
}
static float g_vvol[MAX_VOICES], g_vpan[MAX_VOICES], g_vpitch[MAX_VOICES];   /* game-side copies for partial updates */
static float g_vlp[MAX_VOICES], g_vsend[MAX_VOICES];                            /* low-pass cutoff (Hz, 0 = off), reverb send */
/* per voice: the sound handle it was played with, its bus and priority; variation factors; occlusion 0..1 */
static int g_vsnd[MAX_VOICES], g_vbusn[MAX_VOICES], g_vprio[MAX_VOICES]; static float g_vvary[MAX_VOICES], g_vpvary[MAX_VOICES], g_vocc[MAX_VOICES];
static arc_rng g_arng; static bool g_arng_seeded;                                 /* variation: the audio system's own generator */
static int g_bus_max[ARC_BUS_COUNT], g_bus_steal[ARC_BUS_COUNT];
static arc_occlusion_fn g_occ_fn; static void *g_occ_user;
static volatile float g_occ_gain = 0.4f; static float g_occ_a;                                  /* fully occluded: gain, low-pass coefficient */
static float g_occ_hz = 800.0f; static int g_occ_rate;
/* positional voices: gains follow the listener (recomputed after every tick) */
static bool g_v3d[MAX_VOICES]; static arc_v3 g_vpos[MAX_VOICES]; static float g_vmin[MAX_VOICES], g_vmax[MAX_VOICES], g_vgl[MAX_VOICES], g_vgr[MAX_VOICES];
static arc_v3 g_lis_pos, g_lis_fwd = { 0, 0, -1 }, g_lis_right = { 1, 0, 0 };
static float g_def_min = 1.0f, g_def_max = 40.0f;
static float lp_coef(float hz) { return hz > 0 ? 1.0f - expf(-ARC_TAU * arc_minf(hz, 0.45f * (float)g_rate) / (float)g_rate) : 0.0f; }
/* distance falloff and pan from where the sound is relative to the listener */
static void gains_3d(int slot, float *gl, float *gr) {
    arc_v3 d = arc_v3_sub(g_vpos[slot], g_lis_pos);
    float dist = arc_v3_len(d), mn = arc_maxf(g_vmin[slot], 1e-3f), mx = arc_maxf(g_vmax[slot], mn + 1e-3f);
    float att = (mn / arc_maxf(dist, mn)) * arc_clampf((mx - dist) / (mx - mn), 0, 1);
    float pan = 0, behind = 0;
    if (dist > 1e-4f) { arc_v3 n = arc_v3_scale(d, 1.0f / dist); pan = arc_v3_dot(n, g_lis_right); behind = arc_maxf(0, -arc_v3_dot(n, g_lis_fwd)); }
    pan *= arc_clampf(dist / mn, 0, 1);                     /* very close sounds sit in the middle */
    float a = (pan + 1) * (ARC_PI / 4), v = g_vvol[slot] * g_vvary[slot] * att * (1.0f - 0.25f * behind) * 1.41421356f;
    *gl = v * cosf(a); *gr = v * sinf(a);
}
static bool g_play3d; static arc_v3 g_play3d_pos; static int g_play_clock, g_play_when;   /* stingers: start on a beat of this music */
/* volume groups: every voice plays on a bus (its sound's, or the one given); music on MUSIC; streams on their own */
static volatile float g_bus_vol[ARC_BUS_COUNT] = { 1, 1, 1, 1 };
static int g_play_bus = -1;
void  arc_set_bus_volume(arc_bus bus, float v) { if ((int)bus >= 0 && bus < ARC_BUS_COUNT) g_bus_vol[bus] = arc_clampf(v, 0, 2); }
float arc_bus_volume(arc_bus bus) { return ((int)bus >= 0 && bus < ARC_BUS_COUNT) ? g_bus_vol[bus] : 0; }
void  arc_sound_set_bus(arc_sound s, arc_bus bus) { if (s.id && (int)s.id < g_sound_count && (int)bus >= 0 && bus < ARC_BUS_COUNT) g_sounds[s.id].bus = (int)bus; }
arc_voice arc_play_bus(arc_sound s, arc_bus bus, float volume, float pan) {
    g_play_bus = ((int)bus >= 0 && bus < ARC_BUS_COUNT) ? (int)bus : -1;
    arc_voice v = arc_play_ex(s, volume, pan, 1, false);
    g_play_bus = -1; return v;
}
void arc_stream_set_bus(arc_stream h, arc_bus bus);
float arc__master_volume(void) { return g_master; }
/* a player's sound settings, kept with arc_save under "arc_audio" (master + one per bus) */
bool arc_audio_save_settings(void) {
    float v[1 + ARC_BUS_COUNT] = { g_master }; for (int i = 0; i < ARC_BUS_COUNT; i++) v[1 + i] = g_bus_vol[i];
    return arc_save("arc_audio", v, sizeof v);
}
bool arc_audio_load_settings(void) {
    size_t n = 0; size_t mark = arc_arena_mark(arc_frame());
    uint8_t *d = arc_load("arc_audio", arc_frame(), &n);
    bool ok = d && n == sizeof(float) * (1 + ARC_BUS_COUNT);
    if (ok) { float v[1 + ARC_BUS_COUNT]; memcpy(v, d, sizeof v); arc_set_master_volume(v[0]); for (int i = 0; i < ARC_BUS_COUNT; i++) arc_set_bus_volume((arc_bus)i, v[1 + i]); }
    arc_arena_pop(arc_frame(), mark); return ok;
}
static uint32_t g_sound_alias[MAX_SOUNDS];   /* hot reload: a reloaded sound plays its newest data (old data is kept: voices may use it) */
bool arc__sound_reload(arc_sound old, const char *path) {
    if (!old.id || (int)old.id >= g_sound_count) return false;
    size_t mark = arc_arena_mark(arc_frame()), n = 0;
    uint8_t *d = arc_file_read(path, arc_frame(), &n);
    arc_sound f = d ? arc_sound_from_memory(d, n) : (arc_sound){ 0 };
    arc_arena_pop(arc_frame(), mark);
    if (!f.id) return false;
    g_sound_alias[old.id] = f.id; return true;
}
static float audio_rand01(void) { if (!g_arng_seeded) { arc_rng_seed(&g_arng, 1, 0xA0D10); g_arng_seeded = true; } return arc_rng_float(&g_arng); }
void  arc_audio_seed(uint64_t seed) { arc_rng_seed(&g_arng, seed, 0xA0D10); g_arng_seeded = true; }
float arc_audio_random(float lo, float hi) { return lo + (hi - lo) * audio_rand01(); }
static sound_rec *sound_get(arc_sound s) { return s.id && (int)s.id < g_sound_count ? &g_sounds[s.id] : NULL; }
void arc_sound_set_variation(arc_sound s, float semitones, float volume_range) { sound_rec *r = sound_get(s); if (r) { r->vary_semis = arc_clampf(semitones, 0, 24); r->vary_vol = arc_clampf(volume_range, 0, 1); } }
void arc_sound_set_limit(arc_sound s, int max_voices, arc_steal steal) { sound_rec *r = sound_get(s); if (r) { r->max_voices = arc_clampi(max_voices, 0, MAX_VOICES); r->steal = (int)steal; } }
void arc_sound_set_priority(arc_sound s, int priority) { sound_rec *r = sound_get(s); if (r) r->priority = priority; }
void arc_bus_set_limit(arc_bus bus, int max_voices, arc_steal steal) { if ((int)bus >= 0 && bus < ARC_BUS_COUNT) { g_bus_max[bus] = arc_clampi(max_voices, 0, MAX_VOICES); g_bus_steal[bus] = (int)steal; } }
static float voice_loudness(int i) {   /* game-side estimate, for stealing the quietest */
    float g = g_v3d[i] ? arc_maxf(g_vgl[i], g_vgr[i]) : fabsf(g_vvol[i] * g_vvary[i]);
    return g * (1.0f - g_vocc[i] * (1.0f - g_occ_gain));
}
/* the busy voice to take among those of a sound (snd > 0) or bus (bus >= 0): priority <= prio, lowest first; then quietest or oldest */
static int pick_victim(int snd, int bus, int prio, int steal) {
    int best = -1;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!g_vbusy[i] || (snd > 0 && g_vsnd[i] != snd) || (bus >= 0 && g_vbusn[i] != bus) || g_vprio[i] > prio) continue;
        if (best < 0) { best = i; continue; }
        if (g_vprio[i] != g_vprio[best]) { if (g_vprio[i] < g_vprio[best]) best = i; continue; }
        if (steal == ARC_STEAL_QUIETEST) { float a = voice_loudness(i), b = voice_loudness(best); if (a != b) { if (a < b) best = i; continue; } }
        if (g_vstart[i] < g_vstart[best]) best = i;
    }
    return best;
}
static void stop_slot(int slot) {
    cmd c; memset(&c, 0, sizeof c); c.kind = CMD_VOICE_STOP; c.slot = slot; c.gen = g_vgen[slot];
    g_vbusy[slot] = 0; push(c);
}
/* over a limit (count of voices matching snd / bus >= max): free one, or say no */
static bool make_room(int snd, int bus, int max, int steal, int prio) {
    if (max <= 0) return true;
    int n = 0; for (int i = 0; i < MAX_VOICES; i++) n += g_vbusy[i] && (snd > 0 ? g_vsnd[i] == snd : g_vbusn[i] == bus);
    while (n >= max) {
        int v = steal == ARC_STEAL_NONE ? -1 : pick_victim(snd, bus, prio, steal);
        if (v < 0) return false;
        stop_slot(v); n--;
    }
    return true;
}
arc_voice arc_play_ex(arc_sound s, float volume, float pan, float pitch, bool loop) {
    arc_voice none = { 0 };
    int bus = s.id && (int)s.id < g_sound_count ? g_sounds[s.id].bus : ARC_BUS_SFX;   /* the bus set on the handle the game holds */
    int orig = (int)s.id;
    if (s.id && (int)s.id < g_sound_count && g_sound_alias[s.id]) s.id = g_sound_alias[s.id];
    if (!s.id || (int)s.id >= g_sound_count) return none;
    /* 0.8.1 rollback matches: a re-simulated tick does not play a sound again (the same sound, the n-th time in that tick) */
    arc_voice before = none; int gate = arc__rb_sound_gate((uint32_t)orig, &before);
    if (gate == 1) return before;
    const sound_rec *sr = &g_sounds[orig];   /* limits, priority and variation: also set on the handle the game holds */
    float vary = 1, pvary = 1;               /* drawn on every call (heard or not), so the sequence depends on the game alone */
    if (sr->vary_semis > 0) pvary = powf(2.0f, (audio_rand01() * 2 - 1) * sr->vary_semis / 12.0f);
    if (sr->vary_vol > 0) vary = 1.0f - sr->vary_vol * audio_rand01();
    bool late = gate == 0 && arc__audio_rb;  /* first heard while re-simulating: it starts now, late */
    if ((late ? (arc_headless() && !arc__audio_test_mode) : no_mixer()) || !g_sounds[s.id].data) return none;    /* (0.7.1: still loading: nothing to play) */
    int vbus = g_play_bus >= 0 ? g_play_bus : bus, prio = sr->priority;
    if (!make_room(orig, -1, sr->max_voices, sr->steal, prio)) return none;
    if (!make_room(0, vbus, g_bus_max[vbus], g_bus_steal[vbus], prio)) return none;
    int slot = -1; uint64_t oldest = UINT64_MAX;
    for (int i = 0; i < MAX_VOICES; i++) if (!g_vbusy[i]) { slot = i; break; }
    if (slot < 0) {   /* all busy: reuse the oldest one-shot voice, else the oldest loop (of the lowest priority, at most this play's) */
        int lowest = INT32_MAX; for (int i = 0; i < MAX_VOICES; i++) if (g_vprio[i] < lowest) lowest = g_vprio[i];
        if (lowest > prio) return none;
        for (int pass = 0; pass < 2 && slot < 0; pass++)
            for (int i = 0; i < MAX_VOICES; i++) if (g_vprio[i] == lowest && (pass || !g_vloop[i]) && g_vstart[i] < oldest) { oldest = g_vstart[i]; slot = i; }
    }
    if (slot < 0) return none;
    uint32_t gen = (g_vgen[slot] + 1) & 0xFFFFFF; if (!gen) gen = 1;
    g_vgen[slot] = gen; g_vbusy[slot] = 1; g_vstart[slot] = ++g_vcounter; g_vloop[slot] = loop;
    g_vvol[slot] = volume; g_vpan[slot] = pan; g_vpitch[slot] = pitch > 0 ? pitch : 1; g_vlp[slot] = 0; g_vsend[slot] = 1; g_v3d[slot] = false;
    g_vsnd[slot] = orig; g_vbusn[slot] = vbus; g_vprio[slot] = prio; g_vvary[slot] = vary; g_vpvary[slot] = pvary; g_vocc[slot] = 0;
    cmd c; memset(&c, 0, sizeof c); c.kind = CMD_PLAY; c.id = (int)s.id; c.loop = loop; c.slot = slot; c.gen = gen; c.pitch = g_vpitch[slot] * pvary;
    c.lp = 0; c.send = 1; c.bus = vbus; c.clock = g_play_clock; c.when = g_play_when;
    if (g_play3d) {
        g_v3d[slot] = true; g_vpos[slot] = g_play3d_pos; g_vmin[slot] = g_def_min; g_vmax[slot] = g_def_max; gains_3d(slot, &c.gl, &c.gr); g_vgl[slot] = c.gl; g_vgr[slot] = c.gr;
        if (g_occ_fn) g_vocc[slot] = arc_clampf(g_occ_fn(g_play3d_pos, g_lis_pos, g_occ_user), 0, 1);
    }
    else pan_gains(volume * vary, pan, &c.gl, &c.gr);
    c.occ = g_vocc[slot];
    if (!push(c)) { g_vbusy[slot] = 0; return none; }
    arc_voice v = { (gen << 8) | (uint32_t)(slot + 1) };
    if (gate == 0) arc__rb_sound_voice(v);
    return v;
}
arc_voice arc_play(arc_sound s, float volume, float pan) { return arc_play_ex(s, volume, pan, 1, false); }
static int voice_slot(arc_voice v) {
    int slot = (int)(v.id & 0xFF) - 1;
    if (slot < 0 || slot >= MAX_VOICES || !g_vbusy[slot] || g_vgen[slot] != (v.id >> 8)) return -1;
    return slot;
}
bool arc_voice_playing(arc_voice v) { return voice_slot(v) >= 0; }
void arc_voice_stop(arc_voice v) { int slot = voice_slot(v); if (slot >= 0) stop_slot(slot); }
static void voice_set(int slot) {
    cmd c; memset(&c, 0, sizeof c); c.kind = CMD_VOICE_SET; c.slot = slot; c.gen = g_vgen[slot]; c.pitch = g_vpitch[slot] * g_vpvary[slot];
    c.lp = lp_coef(g_vlp[slot]); c.send = g_vsend[slot]; c.occ = g_vocc[slot];
    if (g_v3d[slot]) { gains_3d(slot, &c.gl, &c.gr); g_vgl[slot] = c.gl; g_vgr[slot] = c.gr; }
    else pan_gains(g_vvol[slot] * g_vvary[slot], g_vpan[slot], &c.gl, &c.gr);
    push(c);
}
arc_voice arc_play_3d_ex(arc_sound s, arc_v3 pos, float volume, float pitch, bool loop) {
    g_play3d = true; g_play3d_pos = pos;
    arc_voice v = arc_play_ex(s, volume, 0, pitch, loop);
    g_play3d = false; return v;
}
arc_voice arc_play_3d(arc_sound s, arc_v3 pos, float volume) { return arc_play_3d_ex(s, pos, volume, 1, false); }
void arc_voice_set_position(arc_voice v, arc_v3 pos) {
    int s = voice_slot(v); if (s < 0) return;
    g_vpos[s] = pos;
    if (!g_v3d[s]) { g_v3d[s] = true; g_vmin[s] = g_def_min; g_vmax[s] = g_def_max; voice_set(s); }   /* becomes positional now */
}
void arc_voice_set_range(arc_voice v, float min_dist, float max_dist) {
    int s = voice_slot(v); if (s < 0) return;
    g_vmin[s] = arc_maxf(min_dist, 1e-3f); g_vmax[s] = arc_maxf(max_dist, g_vmin[s] + 1e-3f);
    if (g_v3d[s]) voice_set(s);
}
void arc_voice_set_lowpass(arc_voice v, float hz) { int s = voice_slot(v); if (s >= 0) { g_vlp[s] = arc_maxf(0, hz); voice_set(s); } }
void arc_voice_set_reverb(arc_voice v, float send) { int s = voice_slot(v); if (s >= 0) { g_vsend[s] = arc_clampf(send, 0, 1); voice_set(s); } }
void arc_set_audio_range(float min_dist, float max_dist) { g_def_min = arc_maxf(min_dist, 1e-3f); g_def_max = arc_maxf(max_dist, g_def_min + 1e-3f); }
void arc_set_listener(arc_v3 pos, arc_v3 forward, arc_v3 up) {
    g_lis_pos = pos;
    arc_v3 f = arc_v3_norm(forward); if (arc_v3_len(f) < 0.5f) f = arc_v3_make(0, 0, -1);
    arc_v3 u = arc_v3_len(up) > 1e-6f ? up : arc_v3_make(0, 1, 0);
    arc_v3 r = arc_v3_norm(arc_v3_cross(f, u)); if (arc_v3_len(r) < 0.5f) r = arc_v3_make(1, 0, 0);
    g_lis_fwd = f; g_lis_right = r;
}
/* after every tick: positional voices follow the listener (only changed gains are sent to the mixer) */
void arc__audio_tick(void) {
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!g_vbusy[i] || !g_v3d[i]) continue;
        float gl, gr; gains_3d(i, &gl, &gr); bool changed = false;
        if (g_occ_fn) { float o = arc_clampf(g_occ_fn(g_vpos[i], g_lis_pos, g_occ_user), 0, 1); if (fabsf(o - g_vocc[i]) > 1e-3f || (o != g_vocc[i] && (o == 0 || o == 1))) { g_vocc[i] = o; changed = true; } }
        if (changed || fabsf(gl - g_vgl[i]) > 1e-4f || fabsf(gr - g_vgr[i]) > 1e-4f) voice_set(i);
    }
}
/* occlusion: a hook for positional voices, or an amount per voice; the mixer glides gain and low-pass to it */
void arc_set_occlusion(arc_occlusion_fn fn, void *user) { g_occ_fn = fn; g_occ_user = user; }
void arc_voice_set_occlusion(arc_voice v, float amount) { int s = voice_slot(v); if (s >= 0) { g_vocc[s] = arc_clampf(amount, 0, 1); voice_set(s); } }
void arc_set_occlusion_filter(float gain, float lowpass_hz) { g_occ_gain = arc_clampf(gain, 0, 1); g_occ_hz = arc_maxf(lowpass_hz, 10); g_occ_rate = 0; }
/* reverb (Freeverb-style: 4 combs + 2 allpasses per ear) and a master low-pass */
static volatile float g_rev_mix, g_rev_room = 0.5f, g_master_lp;
void arc_set_reverb(float mix, float room_size) { g_rev_mix = arc_clampf(mix, 0, 1); g_rev_room = arc_clampf(room_size, 0, 1); }
void arc_set_master_lowpass(float hz) { g_master_lp = arc_maxf(0, hz); }
void arc_voice_set_volume(arc_voice v, float volume) { int s = voice_slot(v); if (s >= 0) { g_vvol[s] = volume; voice_set(s); } }
void arc_voice_set_pan(arc_voice v, float pan) { int s = voice_slot(v); if (s >= 0) { g_vpan[s] = pan; voice_set(s); } }
void arc_voice_set_pitch(arc_voice v, float pitch) { int s = voice_slot(v); if (s >= 0) { g_vpitch[s] = pitch > 0 ? pitch : 1; voice_set(s); } }

/* ---- streams: PCM pushed by the game, played back to back ---- */
static stream_rec *stream_get(arc_stream h) { return h.id && h.id <= MAX_STREAMS && g_streams[h.id - 1].used ? &g_streams[h.id - 1] : NULL; }
void arc_stream_set_bus(arc_stream h, arc_bus bus) { stream_rec *st = stream_get(h); if (st && (int)bus >= 0 && bus < ARC_BUS_COUNT) st->bus = (int)bus; }
arc_stream arc_stream_create(int rate, int channels) {
    arc_stream none = { 0 };
    if (rate <= 0 || channels < 1 || channels > 2) return none;
    for (int i = 0; i < MAX_STREAMS; i++) {
        stream_rec *st = &g_streams[i];
        if (st->used) continue;
        if (st->buf && !st->released) continue;          /* the mixer may still be reading it */
        free(st->buf); memset(st, 0, sizeof *st);
        st->cap = rate * 8; st->channels = channels; st->rate = rate; st->volume = 1; st->bus = ARC_BUS_MUSIC;
        st->buf = (int16_t *)arc__xalloc(sizeof(int16_t) * (size_t)st->cap * channels);
        ARC__BARRIER(); st->used = true;
        arc_stream r = { (uint32_t)i + 1 }; return r;
    }
    arc_log("too many audio streams (%d)", MAX_STREAMS); return none;
}
void arc_stream_push(arc_stream h, const int16_t *pcm, int frames) {
    stream_rec *st = stream_get(h); if (!st || !pcm || frames <= 0 || no_mixer()) return;
    unsigned w = st->w, r = st->r, room = (unsigned)st->cap - (w - r);
    if ((unsigned)frames > room) frames = (int)room;    /* full: the rest is dropped */
    for (int i = 0; i < frames; i++) memcpy(st->buf + (size_t)((w + (unsigned)i) % (unsigned)st->cap) * st->channels, pcm + (size_t)i * st->channels, sizeof(int16_t) * st->channels);
    ARC__BARRIER(); st->w = w + (unsigned)frames;
}
/* headless runs have no mixer: report a full queue so games don't synthesise audio nobody hears */
float arc_stream_queued(arc_stream h) { stream_rec *st = stream_get(h); if (!st) return 0; if (no_mixer()) return 3600.0f; return (float)(st->w - st->r) / (float)st->rate; }
void arc_stream_set_volume(arc_stream h, float v) { stream_rec *st = stream_get(h); if (st) st->volume = arc_clampf(v, 0, 4); }
void arc_stream_clear(arc_stream h) { stream_rec *st = stream_get(h); if (!st) return; cmd c; memset(&c, 0, sizeof c); c.kind = CMD_STREAM_CLEAR; c.id = (int)h.id; push(c); }
void arc_stream_destroy(arc_stream h) {
    stream_rec *st = stream_get(h); if (!st) return;
    st->used = false; st->released = (arc_headless() && !arc__audio_test_mode) ? 1 : 0;   /* a running mixer lets go itself */
    cmd c; memset(&c, 0, sizeof c); c.kind = CMD_STREAM_FREE; c.id = (int)h.id; push(c);
}
static bool stream_pull(stream_rec *st, float out[2]) {
    if (st->r == st->w) return false;
    const int16_t *p = st->buf + (size_t)(st->r % (unsigned)st->cap) * st->channels;
    out[0] = p[0] / 32768.0f; out[1] = st->channels > 1 ? p[1] / 32768.0f : out[0];
    ARC__BARRIER(); st->r = st->r + 1; return true;
}

static void music_restart(music_rec *m);
/* 0.7.1 loader: a slot kept for music still loading, and what the game asked of it meanwhile */
static uint8_t g_music_resv[MAX_MUSIC]; static int8_t g_music_pend_play[MAX_MUSIC]; static float g_music_pend_vol[MAX_MUSIC];
static arc_music music_load_at(int slot, const uint8_t *d, size_t n);
/* ---- music: MP3 or QOA kept compressed in memory, decoded a frame at a time by the mixer ---- */
arc_music arc_music_from_memory(const uint8_t *d, size_t n) {
    arc_music none = { 0 };
    int slot = 1; while (slot < MAX_MUSIC && (g_music[slot].used || g_music_resv[slot])) slot++;
    if (slot >= MAX_MUSIC) { arc_log("too many music tracks (%d)", MAX_MUSIC - 1); return none; }
    return music_load_at(slot, d, n);
}
uint32_t arc__music_reserve(void) {
    int slot = 1; while (slot < MAX_MUSIC && (g_music[slot].used || g_music_resv[slot])) slot++;
    if (slot >= MAX_MUSIC) { arc_log("too many music tracks (%d)", MAX_MUSIC - 1); return 0; }
    g_music_resv[slot] = 1; g_music_pend_play[slot] = 0; g_music_pend_vol[slot] = -1;
    return (uint32_t)slot;
}
bool arc__music_finish(uint32_t id, const uint8_t *d, size_t n) {
    if (!id || id >= MAX_MUSIC || !g_music_resv[id]) return false;
    g_music_resv[id] = 0;
    arc_music m = music_load_at((int)id, d, n);
    if (!m.id) return false;
    if (g_music_pend_vol[id] >= 0) arc_music_set_volume(m, g_music_pend_vol[id]);
    if (g_music_pend_play[id]) arc_music_play(m, g_music_pend_play[id] == 2);
    return true;
}
void arc__music_fail(uint32_t id) { if (id && id < MAX_MUSIC) g_music_resv[id] = 0; }
bool arc_music_ready(arc_music m) { return m.id && m.id < MAX_MUSIC && g_music[m.id].used; }
static arc_music music_load_at(int slot, const uint8_t *d, size_t n) {
    arc_music none = { 0 }; qoa_tables();
    uint8_t *copy = (uint8_t *)arc__xalloc(n); memcpy(copy, d, n);
    qoa_info qi; arc__mp3 *mp = NULL;
    if (qoa_header(copy, n, &qi)) {
        if (qi.channels > 2) { free(copy); arc_log("music must be mono or stereo"); return none; }
    } else {
        mp = (arc__mp3 *)arc__xalloc(arc__mp3_state_size());
        if (!arc__mp3_open(mp, copy, n)) { free(mp); free(copy); arc_log("music must be .mp3 or .qoa"); return none; }
        int64_t len = arc__mp3_length(mp);
        qi.channels = arc__mp3_channels(mp); qi.rate = arc__mp3_rate(mp); qi.frames = len > 0 && len < INT32_MAX ? (int)len : 0;
    }
    music_rec *m = &g_music[slot];
    m->mp3 = mp;
    m->data = copy; m->size = n; m->info = qi; m->volume = 1.0f; m->playing = m->looping = m->paused = m->ended = 0; music_restart(m);
    m->bpm = 0; m->beats_per_bar = 4; m->first_beat = 0; m->lead = m->layer = m->pending = 0; m->pos = 0;
    m->fade.cur = m->fade.target = 1; m->fade.step = 0; m->fade.delay = 0; m->used = true;
    arc_music r = { (uint32_t)slot }; return r;
}
arc_music arc_music_load(const char *path) {
    if (arc__load_queueing()) { arc_music q = { arc__load_queue(ARC__LOAD_MUSIC, path, 0) }; return q; }
    size_t mark = arc_arena_mark(arc_frame()), n = 0;
    uint8_t *d = arc_file_read(path, arc_frame(), &n);
    arc_music m = { 0 };
    if (d) m = arc_music_from_memory(d, n); else arc_log("cannot load music %s", path);
    arc_arena_pop(arc_frame(), mark);
    return m;
}
static music_rec *music(arc_music m) { return m.id && m.id < MAX_MUSIC && g_music[m.id].used ? &g_music[m.id] : NULL; }
/* stems wait (pending) until the one command that starts the whole group reaches the mixer: no stem runs early */
static bool group_arm(int lead, bool loop) {
    bool any = false;
    for (int i = 1; i < MAX_MUSIC; i++) if (g_music[i].used && g_music[i].lead == lead) { music_rec *s = &g_music[i]; s->pending = 1; s->looping = loop; s->paused = 0; s->ended = 0; s->playing = 1; any = true; }
    return any;
}
void arc_music_play(arc_music m, bool loop) {
    if (m.id && m.id < MAX_MUSIC && g_music_resv[m.id]) { if (!arc__audio_silent) g_music_pend_play[m.id] = (int8_t)(loop ? 2 : 1); return; }   /* starts when loaded */
    music_rec *r = music(m); if (!r || arc__audio_silent || r->lead) return;
    if (group_arm((int)m.id, loop)) r->pending = 1;
    r->looping = loop; r->paused = 0; r->ended = 0; r->playing = 1;
    cmd c; memset(&c, 0, sizeof c); c.kind = CMD_MUSIC_PLAY; c.id = (int)m.id; c.loop = loop; push(c);
}
void arc_music_stop(arc_music m) {
    if (m.id && m.id < MAX_MUSIC && g_music_resv[m.id]) g_music_pend_play[m.id] = 0;
    music_rec *r = music(m); if (!r) return;
    r->playing = 0; cmd c; memset(&c, 0, sizeof c); c.kind = CMD_MUSIC_STOP; c.id = (int)m.id; push(c);
}
void  arc_music_pause(arc_music m, bool paused) { music_rec *r = music(m); if (r) r->paused = paused; }   /* stems follow their lead's flag */
bool arc_music_link(arc_music stem, arc_music lead) {
    music_rec *s = music(stem), *l = music(lead); if (!s || stem.id == lead.id) return false;
    if (!lead.id) { s->lead = 0; s->layer = 0; s->pending = 0; s->playing = 0; return true; }
    if (!l || l->lead) return false;
    if (s->info.rate != l->info.rate) { arc_log("music stems must have the same sample rate (%d, %d)", s->info.rate, l->info.rate); return false; }
    if (s->info.frames && l->info.frames && s->info.frames != l->info.frames) arc_log("music stem length differs from its lead (%d, %d frames)", s->info.frames, l->info.frames);
    int top = 0; for (int i = 1; i < MAX_MUSIC; i++) if (g_music[i].used && g_music[i].lead == (int)lead.id && g_music[i].layer > top) top = g_music[i].layer;
    s->pending = 1; s->playing = 0; s->layer = top + 1; ARC__BARRIER(); s->lead = (int)lead.id; return true;   /* silent until the lead next starts */
}
void arc_music_fade(arc_music m, float level, float seconds) {
    if (!music(m)) return;
    cmd c; memset(&c, 0, sizeof c); c.kind = CMD_MUSIC_FADE; c.id = (int)m.id; c.gl = arc_clampf(level, 0, 4); c.gr = seconds; push(c);
}
void arc_music_set_intensity(arc_music lead, float intensity, float seconds, arc_sync when) {
    if (!music(lead)) return;
    cmd c; memset(&c, 0, sizeof c); c.kind = CMD_MUSIC_INTENSITY; c.id = (int)lead.id; c.gl = arc_clampf(intensity, 0, 1); c.gr = seconds; c.when = (int)when; push(c);
}
void arc_music_set_tempo(arc_music m, float bpm, int beats_per_bar, float first_beat) {
    music_rec *r = music(m); if (!r) return;
    r->bpm = arc_maxf(bpm, 0); r->beats_per_bar = beats_per_bar > 0 ? beats_per_bar : 4; r->first_beat = arc_maxf(first_beat, 0);
}
/* 0.10.13 a loop section: decode to start_s once here (the game's thread), keep the decoder's state, and jump back to
   it when the track loops */
void arc_music_set_loop(arc_music mu, float start_s, float end_s) {
    music_rec *m = music(mu); if (!m) return;
    if (start_s <= 0 && end_s <= 0) { m->lp_on = 0; return; }
    int64_t start = (int64_t)((double)arc_maxf(start_s, 0) * m->info.rate + 0.5), end = end_s > 0 ? (int64_t)((double)end_s * m->info.rate + 0.5) : 0;
    if (m->info.frames > 0) { if (start >= m->info.frames) start = 0; if (end > m->info.frames) end = 0; }
    if (end && end <= start) { arc_log("arc_music_set_loop: end %.3f is not after start %.3f", (double)end_s, (double)start_s); return; }
    size_t at = 8; int skip = 0; void *snap = NULL;
    if (m->mp3) {
        arc__mp3 *d = (arc__mp3 *)arc__xalloc(arc__mp3_state_size()), *before = (arc__mp3 *)arc__xalloc(arc__mp3_state_size());
        arc__mp3_open(d, m->data, m->size);
        static int16_t tmp[1152 * 2 * 2]; int64_t acc = 0;
        for (;;) {
            memcpy(before, d, arc__mp3_state_size());
            int k = arc__mp3_read(d, tmp);
            if (k < 0) { memcpy(before, d, arc__mp3_state_size()); arc__mp3_rewind(before); acc = 0; start = 0; break; }
            if (acc + k > start) break;
            acc += k;
        }
        skip = (int)(start - acc); free(d); snap = before;
    } else {
        int64_t acc = 0;
        while (at + 8 <= m->size) {
            const uint8_t *f = m->data + at;
            int fs = (f[4] << 8) | f[5]; size_t sz = (size_t)((f[6] << 8) | f[7]);
            if (fs <= 0 || sz < 8 || at + sz > m->size) break;
            if (acc + fs > start) break;
            acc += fs; at += sz;
        }
        skip = (int)(start - acc);
    }
    void *old_snap = m->lp_snap;
    m->lp_on = 0;   /* (the mixer stops using the old section while it is replaced) */
    m->lp_start = start; m->lp_end = end; m->lp_at = at; m->lp_skip = skip; m->lp_snap = snap;
    m->lp_on = 1;
    (void)old_snap;   /* (kept: the mixer may still be inside it; a few KB per call) */
}
void arc_music_set_loop_fade(arc_music mu, float seconds) {
    music_rec *m = music(mu); if (!m) return;
    int n = (int)(arc_clampf(seconds, 0, 2) * (float)m->info.rate);
    if (n > 0 && !m->tail) {
        music_rec *t = (music_rec *)arc__xalloc(sizeof *t); memset(t, 0, sizeof *t);
        if (m->mp3) t->mp3 = (arc__mp3 *)arc__xalloc(arc__mp3_state_size());
        m->tail = t;
    }
    m->lp_fade = n;
}
float arc_music_position(arc_music m) { music_rec *r = music(m); return r ? r->pos : 0; }
float arc_music_beat(arc_music m) { music_rec *r = music(m); return r && r->bpm > 0 ? (r->pos - r->first_beat) * r->bpm / 60.0f : 0; }
void arc_music_switch(arc_music from, arc_music to, bool loop, arc_sync when, float fade) {
    music_rec *t = music(to); if (!t || arc__audio_silent || t->lead || from.id == to.id) return;
    if (music(from) && g_music[from.id].lead) from.id = (uint32_t)g_music[from.id].lead;   /* a stem's clock is its lead */
    group_arm((int)to.id, loop); t->pending = 1; t->looping = loop; t->paused = 0; t->ended = 0; t->playing = 1;
    cmd c; memset(&c, 0, sizeof c); c.kind = CMD_MUSIC_SWITCH; c.id = (int)to.id; c.clock = music(from) ? (int)from.id : 0; c.when = (int)when; c.gl = fade; push(c);
}
arc_voice arc_music_stinger(arc_music clock, arc_sound s, float volume, arc_sync when) {
    g_play_clock = music(clock) ? (int)clock.id : 0; g_play_when = (int)when; g_play_bus = ARC_BUS_MUSIC;
    arc_voice v = arc_play_ex(s, volume, 0, 1, false);
    g_play_clock = g_play_when = 0; g_play_bus = -1; return v;
}
void  arc_music_set_volume(arc_music m, float v) {
    if (m.id && m.id < MAX_MUSIC && g_music_resv[m.id]) g_music_pend_vol[m.id] = arc_clampf(v, 0, 4);
    music_rec *r = music(m); if (r) r->volume = arc_clampf(v, 0, 4);
}
bool  arc_music_playing(arc_music m) {
    music_rec *r = music(m); if (!r) return false;
    const music_rec *L = r->lead ? &g_music[r->lead] : r;   /* a stem plays while its lead does */
    return r->playing && L->playing && !r->ended && !L->paused;
}
float arc_music_length(arc_music m) { music_rec *r = music(m); return r ? (float)r->info.frames / (float)r->info.rate : 0; }

static bool music_pull_raw(music_rec *m, float out[2]) {      /* next source sample (both channels) */
    bool wrap = m->looping && !m->lp_on;                       /* (with a loop section the caller jumps) */
    if (m->mp3) {
        for (int tries = 0; m->buf_pos >= m->buf_len; tries++) {
            int k = arc__mp3_read(m->mp3, m->buf);
            if (k < 0) {
                if (!wrap || tries > 1) return false;          /* (a file with no audio would loop forever) */
                arc__mp3_rewind(m->mp3); continue;              /* gapless: the trimmed samples join up exactly */
            }
            m->buf_len = k; m->buf_pos = 0;
            if (m->skip_pending > 0) { int d = m->skip_pending < k ? m->skip_pending : k; m->buf_pos = d; m->skip_pending -= d; }
        }
        const int16_t *s = m->buf + (size_t)m->buf_pos * m->info.channels; m->buf_pos++;
        out[0] = s[0] / 32768.0f; out[1] = m->info.channels > 1 ? s[1] / 32768.0f : out[0];
        return true;
    }
    while (m->buf_pos >= m->buf_len) {
        if (m->at >= m->size) {
            if (!wrap) return false;
            m->at = 8;
        }
        int fs = 0; size_t used = qoa_frame(m->data + m->at, m->size - m->at, m->info.channels, m->buf, &fs);
        if (!used || fs <= 0) { if (m->at == 8) return false; m->at = m->size; continue; }
        m->at += used; m->buf_len = fs; m->buf_pos = 0;
        if (m->skip_pending > 0) { int d = m->skip_pending < fs ? m->skip_pending : fs; m->buf_pos = d; m->skip_pending -= d; }
    }
    const int16_t *s = m->buf + (size_t)m->buf_pos * m->info.channels; m->buf_pos++;
    out[0] = s[0] / 32768.0f; out[1] = m->info.channels > 1 ? s[1] / 32768.0f : out[0];
    return true;
}
static void music_jump(music_rec *m) {                         /* to the loop section's start */
    void *snap = m->lp_snap;
    if (m->mp3) { if (snap) memcpy(m->mp3, snap, arc__mp3_state_size()); else arc__mp3_rewind(m->mp3); }
    else m->at = m->lp_at ? m->lp_at : 8;
    m->buf_len = m->buf_pos = 0; m->skip_pending = m->lp_skip; m->next_src = m->lp_start;
}
static bool music_pull(music_rec *m, float out[2]) {
    if (m->lp_on) {
        int64_t end = m->lp_end > 0 ? m->lp_end : (int64_t)m->info.frames;
        if (m->looping && m->lp_fade > 0 && m->tail && !m->tail_n && end > 0 && m->next_src >= end - m->lp_fade && m->next_src < end) {
            /* the crossfade: a copy of the stream plays its last lp_fade samples while this one starts the section again */
            music_rec *t = m->tail; arc__mp3 *keep = t->mp3;
            memcpy(t, m, sizeof *t); t->mp3 = keep; t->tail = NULL; t->lp_on = 0; t->looping = 0;
            if (m->mp3 && keep) memcpy(keep, m->mp3, arc__mp3_state_size());
            m->tail_n = (int)(end - m->next_src);
            music_jump(m);
        } else if (m->looping && m->lp_end > 0 && m->next_src >= m->lp_end) music_jump(m);
    }
    bool ok = music_pull_raw(m, out);
    if (!ok && m->looping && m->lp_on) { music_jump(m); ok = music_pull_raw(m, out); }
    if (ok) m->next_src++;
    if (m->tail_n > 0) {                                       /* equal-power crossfade into the section's start */
        float o2[2]; int left = m->tail_n--;
        float k = (float)left / (float)(m->lp_fade > 0 ? m->lp_fade : 1), a = sqrtf(arc_clampf(k, 0, 1)), b = sqrtf(arc_clampf(1.0f - k, 0, 1));
        if (music_pull_raw(m->tail, o2)) { out[0] = out[0] * b + o2[0] * a; out[1] = out[1] * b + o2[1] * a; }
    }
    return ok;
}
static void music_restart(music_rec *m) {
    m->at = 8; m->buf_len = m->buf_pos = 0; m->t = 0; m->primed = m->draining = false; if (m->mp3) arc__mp3_rewind(m->mp3);
    m->played = 0; m->delay = 0; m->xf.cur = m->xf.target = 1; m->xf.step = 0; m->xf.delay = 0; m->xf_stop = false;
    m->next_src = 0; m->i0 = m->i1 = 0; m->skip_pending = 0; m->tail_n = 0;
}

#define REV_COMBS 4
#define REV_MAXLEN 3200
typedef struct { float buf[REV_MAXLEN]; int len, at; float store; } rev_comb;
typedef struct { float buf[REV_MAXLEN]; int len, at; } rev_ap;
static rev_comb g_comb[2][REV_COMBS]; static rev_ap g_ap[2][2]; static int g_rev_rate; static float g_mlp[2];
static void reverb_setup(void) {
    static const int CL[REV_COMBS] = { 1116, 1188, 1277, 1356 }, AL[2] = { 556, 441 };
    g_rev_rate = g_rate;
    for (int ch = 0; ch < 2; ch++) {
        for (int i = 0; i < REV_COMBS; i++) { memset(&g_comb[ch][i], 0, sizeof g_comb[ch][i]); g_comb[ch][i].len = arc_clampi((int)(((float)(CL[i] + ch * 23)) * (float)g_rate / 44100.0f), 16, REV_MAXLEN); }
        for (int i = 0; i < 2; i++) { memset(&g_ap[ch][i], 0, sizeof g_ap[ch][i]); g_ap[ch][i].len = arc_clampi((int)(((float)(AL[i] + ch * 23)) * (float)g_rate / 44100.0f), 16, REV_MAXLEN); }
    }
}
static float reverb_ch(int ch, float in, float fb) {
    float o = 0, damp = 0.3f;
    for (int i = 0; i < REV_COMBS; i++) {
        rev_comb *c = &g_comb[ch][i]; float y = c->buf[c->at];
        c->store = y * (1 - damp) + c->store * damp;
        c->buf[c->at] = in + c->store * fb; if (++c->at >= c->len) c->at = 0;
        o += y;
    }
    for (int i = 0; i < 2; i++) {
        rev_ap *a = &g_ap[ch][i]; float b = a->buf[a->at], y = -o + b;
        a->buf[a->at] = o + b * 0.5f; if (++a->at >= a->len) a->at = 0;
        o = y;
    }
    return o;
}
static void mix_chunk(float *out, int frames);
void arc__audio_mix(float *out, int frames) {
    double t0 = arc__plat_time();
    while (frames > 0) { int n = frames < 2048 ? frames : 2048; mix_chunk(out, n); out += n * 2; frames -= n; }
    arc__audio_mix_ms += (float)((arc__plat_time() - t0) * 1000.0);   /* frame budget (display only) */
}
/* ---- adaptive music and ducking (mixer side) ---- */
static void ramp_go(music_ramp *r, float target, float seconds, int delay) {
    r->delay = delay; r->target = target;
    r->step = seconds > 0 ? arc_maxf(fabsf(target - r->cur), 1e-6f) / (seconds * (float)g_rate) : 1e9f;
}
static bool ramp_on(const music_ramp *r) { return r->cur != 1.0f || r->target != r->cur; }
static void ramp_tick(music_ramp *r) {
    if (r->delay > 0) { r->delay--; return; }
    if (r->cur < r->target) { r->cur += r->step; if (r->cur > r->target) r->cur = r->target; }
    else if (r->cur > r->target) { r->cur -= r->step; if (r->cur < r->target) r->cur = r->target; }
}
/* output frames from the start of this chunk until the next beat / bar of a music track (0: at once) */
static int sync_delay(int clock, int when) {
    if (when == ARC_SYNC_NOW || clock <= 0 || clock >= MAX_MUSIC) return 0;
    music_rec *m = &g_music[clock];
    if (!m->used || m->bpm <= 0 || !m->playing || m->paused || m->ended || m->info.rate <= 0) return 0;
    double rate = m->info.rate, step = rate / g_rate, len = 60.0 / m->bpm * rate * (when == ARC_SYNC_BAR ? m->beats_per_bar : 1);
    double first = m->first_beat * rate, L = m->info.frames, p = m->primed ? (double)m->played + m->t : 0;
    if (L > 0) p = fmod(p, L);
    double k = ceil((p - first - 1e-3) / len); if (k < 0) k = 0;   /* (beats within 1/1000 frame count as now) */
    double b = first + k * len;
    if (L > 0 && b >= L) b = m->looping ? L + first : p;          /* past the last beat: the next loop's first (or now: it ends) */
    double d = ceil((b - p - 1e-3) / step); if (d < 0) d = 0; if (d > 1e9) d = 1e9;
    return m->delay + (int)d;
}
static int stem_count(int lead) { int n = 0; for (int i = 1; i < MAX_MUSIC; i++) n += g_music[i].used && g_music[i].lead == lead; return n; }
static void music_cmd(const cmd *c) {
    if (c->kind == CMD_MUSIC_PLAY) {
        for (int i = 1; i < MAX_MUSIC; i++) if (g_music[i].used && g_music[i].lead == c->id) { music_restart(&g_music[i]); ARC__BARRIER(); g_music[i].pending = 0; }
        ARC__BARRIER(); g_music[c->id].pending = 0;
    } else if (c->kind == CMD_MUSIC_FADE) ramp_go(&g_music[c->id].fade, c->gl, c->gr, 0);
    else if (c->kind == CMD_MUSIC_INTENSITY) {            /* stem k of n: 0 below (k-1)/n, 1 above k/n */
        int n = stem_count(c->id), d = sync_delay(c->id, c->when);
        for (int i = 1; i < MAX_MUSIC; i++) {
            music_rec *m = &g_music[i]; if (!m->used || m->lead != c->id) continue;
            int k = 1; for (int j = 1; j < MAX_MUSIC; j++) k += g_music[j].used && g_music[j].lead == c->id && g_music[j].layer < m->layer;
            ramp_go(&m->fade, arc_clampf(c->gl * (float)n - (float)(k - 1), 0, 1), c->gr, d);
        }
    } else if (c->kind == CMD_MUSIC_SWITCH) {
        int d = sync_delay(c->clock, c->when); float fade = c->gl;
        for (int i = 1; i < MAX_MUSIC; i++) {
            music_rec *m = &g_music[i]; if (!m->used) continue;
            if (i == c->id || m->lead == c->id) {             /* the new piece: from the top, after d frames, fading in */
                music_restart(m); m->delay = d;
                if (fade > 0) { m->xf.cur = 0; ramp_go(&m->xf, 1, fade, 0); }
                ARC__BARRIER(); m->pending = 0;
            } else if (c->clock && (i == c->clock || m->lead == c->clock)) { ramp_go(&m->xf, 0, fade, d); m->xf_stop = true; }
        }
    }
}
static bool music_live(const music_rec *m) {
    const music_rec *L = m->lead ? &g_music[m->lead] : m;   /* stems follow their lead's play / pause */
    return m->used && L->playing && !L->paused && !m->ended && !m->pending;
}
/* ducking: per bus, the gain for each frame of this chunk (on = false: no ducking, 1 everywhere) */
static volatile float g_duck_db[ARC_BUS_COUNT], g_duck_att[ARC_BUS_COUNT], g_duck_rel[ARC_BUS_COUNT], g_duck_now[ARC_BUS_COUNT];
static volatile unsigned g_duck_mask[ARC_BUS_COUNT];
static float g_duck_cur[ARC_BUS_COUNT], g_dg[ARC_BUS_COUNT][2048]; static bool g_duck_on[ARC_BUS_COUNT];
void arc_bus_duck(arc_bus bus, unsigned trigger_buses, float db, float attack, float release) {
    if ((int)bus < 0 || bus >= ARC_BUS_COUNT) return;
    g_duck_mask[bus] = trigger_buses & ~(1u << bus); g_duck_att[bus] = arc_maxf(attack, 0); g_duck_rel[bus] = arc_maxf(release, 0); g_duck_db[bus] = arc_clampf(db, 0, 96);
}
float arc_bus_ducked(arc_bus bus) { return ((int)bus >= 0 && bus < ARC_BUS_COUNT) ? g_duck_now[bus] : 0; }
static void duck_chunk(int frames) {
    unsigned playing = 0;   /* buses with something audible on them */
    for (int i = 0; i < MAX_VOICES; i++) if (g_voices[i].sound && !g_voices[i].delay) playing |= 1u << g_voices[i].bus;
    for (int i = 0; i < MAX_STREAMS; i++) { stream_rec *st = &g_streams[i]; if (st->used && st->buf && (st->w != st->r || st->primed)) playing |= 1u << arc_clampi(st->bus, 0, ARC_BUS_COUNT - 1); }
    for (int i = 1; i < MAX_MUSIC; i++) if (music_live(&g_music[i]) && !g_music[i].delay) playing |= 1u << ARC_BUS_MUSIC;
    for (int b = 0; b < ARC_BUS_COUNT; b++) {
        float db = g_duck_db[b], cur = g_duck_cur[b];
        g_duck_on[b] = db > 0 || cur > 0;
        if (!g_duck_on[b]) continue;
        float target = (playing & g_duck_mask[b]) ? db : 0, full = arc_maxf(db, cur), rate = (float)g_rate;
        float up = g_duck_att[b] > 0 ? full / (g_duck_att[b] * rate) : 1e9f, down = g_duck_rel[b] > 0 ? full / (g_duck_rel[b] * rate) : 1e9f;
        float gain = powf(10.0f, -cur / 20.0f);
        for (int i = 0; i < frames; i++) {
            if (cur != target) { cur = cur < target ? arc_minf(cur + up, target) : arc_maxf(cur - down, target); gain = powf(10.0f, -cur / 20.0f); }
            g_dg[b][i] = gain;
        }
        g_duck_cur[b] = cur; g_duck_now[b] = cur;
    }
}
static void mix_chunk(float *out, int frames) {
    static float wet[2048 * 2];
    float rev_mix = g_rev_mix;
    if (rev_mix > 0 && g_rev_rate != g_rate) reverb_setup();
    if (rev_mix > 0) memset(wet, 0, sizeof(float) * 2 * (size_t)frames);
    if (g_occ_rate != g_rate) { g_occ_a = lp_coef(g_occ_hz); g_occ_rate = g_rate; }
    while (g_ring_r != g_ring_w) {
        cmd c = g_ring[g_ring_r % RING]; ARC__BARRIER(); g_ring_r++;
        if (c.kind == CMD_PLAY) {
            voice *v = &g_voices[c.slot]; v->sound = c.id; v->pos = 0; v->gl = c.gl; v->gr = c.gr; v->gen = c.gen; v->loop = c.loop != 0;
            v->lp_a = c.lp; v->lp_l = v->lp_r = 0; v->send = c.send; v->bus = arc_clampi(c.bus, 0, ARC_BUS_COUNT - 1);
            v->step = (double)g_sounds[c.id].rate / g_rate * c.pitch;
            v->delay = sync_delay(c.clock, c.when); v->occ = v->occ_t = c.occ; v->oz[0] = v->oz[1] = v->oz[2] = v->oz[3] = 0;
        } else if (c.kind == CMD_VOICE_STOP) { voice *v = &g_voices[c.slot]; if (v->gen == c.gen) v->sound = 0; }
        else if (c.kind == CMD_VOICE_SET) {
            voice *v = &g_voices[c.slot];
            if (v->gen == c.gen && v->sound) { v->gl = c.gl; v->gr = c.gr; v->step = (double)g_sounds[v->sound].rate / g_rate * c.pitch; v->lp_a = c.lp; v->send = c.send; v->occ_t = c.occ; }
        } else if (c.kind == CMD_STREAM_CLEAR) { stream_rec *st = &g_streams[c.id - 1]; st->r = st->w; st->primed = false; }
        else if (c.kind == CMD_STREAM_FREE) { stream_rec *st = &g_streams[c.id - 1]; st->primed = false; ARC__BARRIER(); st->released = 1; }
        else if (c.kind == CMD_MUSIC_PLAY && g_music[c.id].used) { music_restart(&g_music[c.id]); music_cmd(&c); }
        else if (c.kind >= CMD_MUSIC_FADE && g_music[c.id].used) music_cmd(&c);
    }
    duck_chunk(frames);
    memset(out, 0, sizeof(float) * 2 * (size_t)frames);
    float occ_glide = 20.0f / (float)g_rate, occ_a = g_occ_a, occ_g = g_occ_gain;   /* occlusion: 0..1 in 50 ms */
    for (int vi = 0; vi < MAX_VOICES; vi++) {
        voice *vc = &g_voices[vi]; if (!vc->sound) continue;
        const sound_rec *s = &g_sounds[vc->sound]; int ch = s->channels, start = 0;
        if (vc->delay > 0) { start = vc->delay < frames ? vc->delay : frames; vc->delay -= start; }   /* starts on a beat */
        const float *dg = g_duck_on[vc->bus] ? g_dg[vc->bus] : NULL;
        for (int i = start; i < frames; i++) {
            int i0 = (int)vc->pos;
            if (i0 >= s->frames) {
                if (vc->loop) { vc->pos -= (double)s->frames; i0 = (int)vc->pos; if (i0 < 0 || i0 >= s->frames) { vc->pos = 0; i0 = 0; } }
                else { vc->sound = 0; if (g_vgen[vi] == vc->gen) g_vbusy[vi] = 0; break; }
            }
            int i1 = i0 + 1 < s->frames ? i0 + 1 : (vc->loop ? 0 : i0); float t = (float)(vc->pos - i0);
            float l = (s->data[i0 * ch] * (1 - t) + s->data[i1 * ch] * t) / 32768.0f;
            float r = ch > 1 ? (s->data[i0 * ch + 1] * (1 - t) + s->data[i1 * ch + 1] * t) / 32768.0f : l;
            if (vc->lp_a > 0) { vc->lp_l += (l - vc->lp_l) * vc->lp_a; vc->lp_r += (r - vc->lp_r) * vc->lp_a; l = vc->lp_l; r = vc->lp_r; }
            if (vc->occ > 0 || vc->occ_t > 0) {               /* occlusion: two one-pole low-passes and a gain, gliding */
                float o = vc->occ, dto = vc->occ_t - o; o += dto > occ_glide ? occ_glide : dto < -occ_glide ? -occ_glide : dto; vc->occ = o;
                float a = 1.0f - o * (1.0f - occ_a), g = 1.0f - o * (1.0f - occ_g);   /* o = 0: a = 1, g = 1 (passes through) */
                vc->oz[0] += (l - vc->oz[0]) * a; vc->oz[1] += (vc->oz[0] - vc->oz[1]) * a;
                vc->oz[2] += (r - vc->oz[2]) * a; vc->oz[3] += (vc->oz[2] - vc->oz[3]) * a;
                l = vc->oz[1] * g; r = vc->oz[3] * g;
            }
            float bv = g_bus_vol[vc->bus]; if (dg) bv *= dg[i];
            l *= vc->gl * bv; r *= vc->gr * bv;
            out[i * 2] += l; out[i * 2 + 1] += r;
            if (rev_mix > 0) { wet[i * 2] += l * vc->send; wet[i * 2 + 1] += r * vc->send; }
            vc->pos += vc->step;
        }
    }
    for (int mi = 1; mi < MAX_MUSIC; mi++) {
        music_rec *m = &g_music[mi];
        if (!music_live(m)) continue;
        int i0 = 0;
        if (m->delay > 0) { i0 = m->delay < frames ? m->delay : frames; m->delay -= i0; if (i0 >= frames) continue; }   /* starts on a beat */
        float vol = m->volume * g_bus_vol[ARC_BUS_MUSIC]; double step = (double)m->info.rate / g_rate;
        if (!m->primed) { if (!music_pull(m, m->s0) || !music_pull(m, m->s1)) { m->ended = 1; continue; } m->primed = true; m->i0 = m->next_src - 2; m->i1 = m->next_src - 1; }
        const float *dg = g_duck_on[ARC_BUS_MUSIC] ? g_dg[ARC_BUS_MUSIC] : NULL;
        bool fx = dg || ramp_on(&m->fade) || ramp_on(&m->xf);
        for (int i = i0; i < frames; i++) {
            float t = (float)m->t;
            if (!fx) {
                out[i * 2] += (m->s0[0] + (m->s1[0] - m->s0[0]) * t) * vol;
                out[i * 2 + 1] += (m->s0[1] + (m->s1[1] - m->s0[1]) * t) * vol;
            } else {                                          /* fades, crossfades, ducking */
                ramp_tick(&m->fade); ramp_tick(&m->xf);
                if (m->xf_stop && m->xf.cur <= 0 && m->xf.target <= 0 && !m->xf.delay) { m->ended = 1; break; }
                float g = vol * m->fade.cur * m->xf.cur; if (dg) g *= dg[i];
                out[i * 2] += (m->s0[0] + (m->s1[0] - m->s0[0]) * t) * g;
                out[i * 2 + 1] += (m->s0[1] + (m->s1[1] - m->s0[1]) * t) * g;
            }
            m->t += step;
            while (m->t >= 1.0) {
                m->t -= 1.0;
                if (m->draining) { m->ended = 1; break; }          /* the last sample has been played */
                m->s0[0] = m->s1[0]; m->s0[1] = m->s1[1]; m->played++; m->i0 = m->i1;
                if (!music_pull(m, m->s1)) { m->s1[0] = m->s0[0]; m->s1[1] = m->s0[1]; m->draining = true; }
                else m->i1 = m->next_src - 1;
            }
            if (m->ended) break;
        }
        double p = (double)m->played + m->t; if (m->info.frames > 0) p = fmod(p, (double)m->info.frames);
        if (m->lp_on) p = (double)m->i0 + m->t;                 /* 0.10.13: where in the file (the loop jumps back) */
        m->pos = (float)(p / m->info.rate);
    }
    for (int si = 0; si < MAX_STREAMS; si++) {
        stream_rec *st = &g_streams[si];
        if (!st->used || !st->buf) continue;
        int sb = arc_clampi(st->bus, 0, ARC_BUS_COUNT - 1); const float *dg = g_duck_on[sb] ? g_dg[sb] : NULL;
        float vol = st->volume * g_bus_vol[sb]; double step = (double)st->rate / g_rate;
        if (!st->primed) { if (st->w - st->r < 2) continue; stream_pull(st, st->s0); stream_pull(st, st->s1); st->t = 0; st->primed = true; }
        for (int i = 0; i < frames; i++) {
            float t = (float)st->t, g = dg ? vol * dg[i] : vol;
            out[i * 2] += (st->s0[0] + (st->s1[0] - st->s0[0]) * t) * g;
            out[i * 2 + 1] += (st->s0[1] + (st->s1[1] - st->s0[1]) * t) * g;
            st->t += step;
            while (st->t >= 1.0) {
                st->t -= 1.0; st->s0[0] = st->s1[0]; st->s0[1] = st->s1[1];
                if (!stream_pull(st, st->s1)) { st->primed = false; break; }   /* ran dry: wait for more */
            }
            if (!st->primed) break;
        }
    }
    if (rev_mix > 0) {
        float fb = 0.70f + 0.28f * g_rev_room, gain = 0.12f * rev_mix;
        for (int i = 0; i < frames; i++) {
            float in = (wet[i * 2] + wet[i * 2 + 1]) * 0.5f;
            out[i * 2] += reverb_ch(0, in, fb) * gain; out[i * 2 + 1] += reverb_ch(1, in, fb) * gain;
        }
    }
    float mlp = g_master_lp;
    if (mlp > 0) {
        float a = lp_coef(mlp);
        for (int i = 0; i < frames; i++) { g_mlp[0] += (out[i * 2] - g_mlp[0]) * a; g_mlp[1] += (out[i * 2 + 1] - g_mlp[1]) * a; out[i * 2] = g_mlp[0]; out[i * 2 + 1] = g_mlp[1]; }
    }
    float mv = g_master;
    for (int i = 0; i < frames * 2; i++) { float x = out[i] * mv; out[i] = x > 1 ? 1 : x < -1 ? -1 : x; }
}

/* decode a whole .qoa for tests and tools: returns interleaved samples (free() them), NULL on error */
int16_t *arc__qoa_decode_all(const uint8_t *d, size_t n, int *channels, int *rate, int *frames) {
    qoa_tables(); qoa_info qi; if (!qoa_header(d, n, &qi)) return NULL;
    int16_t *out = (int16_t *)arc__xalloc(sizeof(int16_t) * (size_t)qi.frames * qi.channels + QOA_FRAME_LEN * 8 * 2);
    size_t at = 8; int got = 0;
    while (got < qi.frames && at < n) { int fs = 0; size_t used = qoa_frame(d + at, n - at, qi.channels, out + (size_t)got * qi.channels, &fs); if (!used) break; at += used; got += fs; }
    *channels = qi.channels; *rate = qi.rate; *frames = got < qi.frames ? got : qi.frames; return out;
}
