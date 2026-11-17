/* QOA encoder for ARC's audio tools. Standalone (no ARC, no libraries).
 *   qoa_encode IN.wav OUT.qoa        IN: 16-bit PCM WAV, 1..8 channels, any rate
 * Usually run by tools/audio_convert.py, which first decodes MP3/OGG/etc. to WAV with ffmpeg.
 * Follows the reference encoder (qoa.h, Dominic Szablewski, MIT): per 20-sample slice, try all 16
 * scale factors and keep the one with the smallest error, with a penalty on runaway LMS weights.
 * Build: cc -O2 tools/qoa_encode.c -o tools/bin/qoa_encode -lm */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SLICE 20
#define FRAME_SLICES 256
#define FRAME_LEN (SLICE * FRAME_SLICES)

typedef struct { int history[4], weights[4]; } lms_t;
static const int recip[16] = { 65536, 9363, 3121, 1457, 781, 475, 311, 216, 156, 117, 90, 71, 57, 47, 39, 32 };
static const int quant[17] = { 7, 7, 7, 5, 5, 3, 3, 1, 0, 0, 2, 2, 4, 4, 6, 6, 6 };
static int dequant[16][8];

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int qdiv(int v, int sf) {
    int r = recip[sf], n = (v * r + (1 << 15)) >> 16;
    return n + ((v > 0) - (v < 0)) - ((n > 0) - (n < 0));   /* round away from zero */
}
static int predict(const lms_t *l) { int p = 0; for (int i = 0; i < 4; i++) p += l->weights[i] * l->history[i]; return p >> 13; }
static void update(lms_t *l, int sample, int residual) {
    int delta = residual >> 4;
    for (int i = 0; i < 4; i++) l->weights[i] += l->history[i] < 0 ? -delta : delta;
    l->history[0] = l->history[1]; l->history[1] = l->history[2]; l->history[2] = l->history[3]; l->history[3] = sample;
}
static void put64(FILE *f, uint64_t v) { for (int i = 7; i >= 0; i--) fputc((int)((v >> (i * 8)) & 0xff), f); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: qoa_encode IN.wav OUT.qoa\n"); return 2; }
    FILE *in = fopen(argv[1], "rb"); if (!in) { perror(argv[1]); return 1; }
    fseek(in, 0, SEEK_END); long n = ftell(in); fseek(in, 0, SEEK_SET);
    uint8_t *d = (uint8_t *)malloc((size_t)n); if (!d || fread(d, 1, (size_t)n, in) != (size_t)n) { fprintf(stderr, "read failed\n"); return 1; }
    fclose(in);
    if (n < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) { fprintf(stderr, "%s: not a WAV file\n", argv[1]); return 1; }
    int ch = 0, rate = 0, bits = 0; const int16_t *pcm = NULL; uint32_t frames = 0; long at = 12;
    while (at + 8 <= n) {
        uint32_t len = le32(d + at + 4); const uint8_t *body = d + at + 8;
        if (at + 8 + (long)len > n) len = (uint32_t)(n - at - 8);
        if (!memcmp(d + at, "fmt ", 4)) { ch = body[2] | body[3] << 8; rate = (int)le32(body + 4); bits = body[14] | body[15] << 8; }
        else if (!memcmp(d + at, "data", 4) && ch) { pcm = (const int16_t *)body; frames = len / (uint32_t)(2 * ch); break; }
        at += 8 + len + (len & 1);
    }
    if (!pcm || bits != 16 || ch < 1 || ch > 8 || rate <= 0 || !frames) { fprintf(stderr, "%s: need 16-bit PCM, 1-8 channels\n", argv[1]); return 1; }

    static const float base[8] = { 0.75f, -0.75f, 2.5f, -2.5f, 4.5f, -4.5f, 7.0f, -7.0f };
    for (int s = 0; s < 16; s++) { float sf = roundf(powf((float)(s + 1), 2.75f)); for (int q = 0; q < 8; q++) { float v = sf * base[q]; dequant[s][q] = (int)(v < 0 ? v - 0.5f : v + 0.5f); } }

    FILE *out = fopen(argv[2], "wb"); if (!out) { perror(argv[2]); return 1; }
    fwrite("qoaf", 1, 4, out);
    for (int i = 3; i >= 0; i--) fputc((int)((frames >> (i * 8)) & 0xff), out);
    lms_t lms[8]; int prev_sf[8] = { 0 };
    for (int c = 0; c < ch; c++) { memset(&lms[c], 0, sizeof lms[c]); lms[c].weights[2] = -(1 << 13); lms[c].weights[3] = 1 << 14; }
    double err_sum = 0, sig_sum = 0;
    for (size_t i = 0; i < (size_t)frames * ch; i++) sig_sum += (double)pcm[i] * pcm[i];
    for (uint32_t fstart = 0; fstart < frames; fstart += FRAME_LEN) {
        int flen = (int)(frames - fstart < FRAME_LEN ? frames - fstart : FRAME_LEN);
        int slices = (flen + SLICE - 1) / SLICE;
        uint32_t fsize = 8 + (uint32_t)ch * 16 + (uint32_t)slices * ch * 8;
        put64(out, (uint64_t)ch << 56 | (uint64_t)rate << 32 | (uint64_t)flen << 16 | fsize);
        for (int c = 0; c < ch; c++) {
            uint64_t h = 0, w = 0;
            for (int i = 0; i < 4; i++) { h = (h << 16) | (uint16_t)(int16_t)lms[c].history[i]; w = (w << 16) | (uint16_t)(int16_t)lms[c].weights[i]; }
            put64(out, h); put64(out, w);
        }
        const int16_t *fs = pcm + (size_t)fstart * ch;
        for (int si = 0; si < flen; si += SLICE)
            for (int c = 0; c < ch; c++) {
                int slen = flen - si < SLICE ? flen - si : SLICE;
                uint64_t best_rank = UINT64_MAX, best_slice = 0, best_err = 0; lms_t best_lms = lms[c]; int best_sf = 0;
                for (int k = 0; k < 16; k++) {
                    int sf = (k + prev_sf[c]) % 16;
                    lms_t l = lms[c]; uint64_t slice = (uint64_t)sf, rank = 0, err = 0;
                    for (int s = si; s < si + slen; s++) {
                        int sample = fs[s * ch + c], pred = predict(&l), residual = sample - pred;
                        int q = quant[clampi(qdiv(residual, sf), -8, 8) + 8], dq = dequant[sf][q];
                        int rec = clampi(pred + dq, -32768, 32767);
                        long long wp = (((long long)l.weights[0] * l.weights[0] + (long long)l.weights[1] * l.weights[1] +
                                        (long long)l.weights[2] * l.weights[2] + (long long)l.weights[3] * l.weights[3]) >> 18) - 0x8ff;
                        if (wp < 0) wp = 0;
                        long long e = sample - rec; uint64_t esq = (uint64_t)(e * e);
                        rank += esq + (uint64_t)(wp * wp); err += esq;
                        if (rank > best_rank) break;
                        update(&l, rec, dq);
                        slice = (slice << 3) | (uint64_t)q;
                    }
                    if (rank < best_rank) { best_rank = rank; best_err = err; best_slice = slice; best_lms = l; best_sf = sf; }
                }
                prev_sf[c] = best_sf; lms[c] = best_lms; err_sum += (double)best_err;
                best_slice <<= (uint64_t)(SLICE - slen) * 3;
                put64(out, best_slice);
            }
    }
    fclose(out);
    double rms = sqrt(err_sum / ((double)frames * ch)), snr = err_sum > 0 ? 10.0 * log10(sig_sum / err_sum) : 99.0;
    printf("%s: %u frames x %d ch @ %d Hz, rms error %.1f, snr %.1f dB\n", argv[2], frames, ch, rate, rms, snr);
    free(d); return 0;
}
