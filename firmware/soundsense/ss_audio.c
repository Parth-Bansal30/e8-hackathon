/* Sound Sense raw-audio tap (see ss_audio.h). */
#include "ss_audio.h"

#include <math.h>
#include <string.h>

#include "kws.h"
#include "ss_board.h"

#define NBLK_RING (SS_AUDIO_RING / KWS_HOP)   /* 100 blocks */
#define EVQ 16

static int16_t ring[SS_AUDIO_RING];
static uint32_t blk_cyc[NBLK_RING];           /* completion time of each block in the ring */
static volatile uint32_t n_samples;           /* total samples written */
static float floor_pow = 1e-4f, block_db = -120.0f;   /* noise floor as mean power, full scale = 1 */
static uint32_t blocks, refract_until;
static ss_onset evq[EVQ];
static uint32_t ev_w, ev_r;
static ss_audio_tap_fn tap;
static uint16_t cal_hist[100];                /* S5: block levels, 1 dB bins from -100 dBFS */
static uint32_t cal_left, cal_total;
static int cal_done;
static float cal_db;

void ss_audio_init(void)
{
    n_samples = 0;
    blocks = 0;
    ev_w = ev_r = 0;
    floor_pow = 1e-4f;
}

void ss_audio_set_tap(ss_audio_tap_fn fn) { tap = fn; }
uint32_t ss_audio_samples(void) { return n_samples; }
float ss_audio_block_db(void) { return block_db; }
float ss_audio_floor_db(void) { return 10.0f * log10f(floor_pow + 1e-12f); }

SS_COLD void ss_audio_room_cal(uint32_t n_blocks)
{
    for (int i = 0; i < 100; i++) cal_hist[i] = 0;
    cal_done = 0;
    cal_total = cal_left = n_blocks;
}

SS_COLD int ss_audio_room_cal_state(float *frac, float *db)
{
    if (cal_left) {
        *frac = 1.0f - (float)cal_left / (float)cal_total;
        return 1;
    }
    if (cal_done) {
        cal_done = 0;
        *db = cal_db;
        return 2;
    }
    return 0;
}

SS_COLD static void room_cal_finish(void)
{
    uint32_t n = 0;
    int m = 0;
    while (m < 99 && (n += cal_hist[m]) * 2 < cal_total) m++;
    cal_db = (float)m - 99.5f;            /* bin m holds -100+m .. -99+m dBFS */
    floor_pow = expf(cal_db * 0.230258509f);   /* 10^(dB/10) */
    cal_done = 1;
}

uint32_t ss_audio_sample_cyc(uint32_t sample)
{
    const uint32_t blk = sample / KWS_HOP, off = sample % KWS_HOP;
    const uint32_t per = SystemCoreClock / KWS_SR;
    return blk_cyc[blk % NBLK_RING] - (uint32_t)(KWS_HOP - 1 - off) * per;
}

int ss_audio_copy(uint32_t first, int16_t *out, uint32_t n)
{
    const uint32_t end = n_samples;
    if (first + n > end || end - first > SS_AUDIO_RING) return -1;
    for (uint32_t i = 0; i < n; i++) out[i] = ring[(first + i) % SS_AUDIO_RING];
    return 0;
}

void kws_block_hook(const int16_t *stereo, int n, uint32_t done_cycles)
{
    const uint32_t base = n_samples;
    float sum_sq = 0.0f, peak = 0.0f;
    int first_over = -1;
    const float floor_rms = sqrtf(floor_pow), thr = SS_ONSET_RATIO * floor_rms;
    for (int i = 0; i < n; i++) {
        const int16_t v = stereo[2 * i];   /* mic 4, as kws.c */
        ring[(base + i) % SS_AUDIO_RING] = v;
        const float a = fabsf(v / 32768.0f);
        sum_sq += a * a;
        if (a > peak) peak = a;
        if (first_over < 0 && a > thr) first_over = i;
    }
    blk_cyc[(base / KWS_HOP) % NBLK_RING] = done_cycles;
    n_samples = base + (uint32_t)n;
    blocks++;
    block_db = sum_sq > 0.0f ? 10.0f * log10f(sum_sq / n) : -120.0f;

    const float pow_blk = sum_sq / n;
    if (first_over >= 0 && base + (uint32_t)first_over >= refract_until && blocks > 25) {
        if (ev_w - ev_r < EVQ) {
            ss_onset *o = &evq[ev_w++ % EVQ];
            o->sample = base + (uint32_t)first_over;
            o->cyc = ss_audio_sample_cyc(o->sample);
            o->peak = peak;
            o->floor = floor_rms;
        }
        refract_until = base + (uint32_t)first_over + SS_ONSET_REFRACT_MS * (KWS_SR / 1000);
    }
    /* Floor = mean power of the background. Blocks within 10 dB of it update it over about 1 s; louder
     * blocks over about 20 s, so a steady sound slowly becomes the background but short events don't
     * move it. The first 0.5 s set it quickly. (The PDM samples come in steps of 128 LSB; an RMS floor
     * with a 6x threshold doesn't fire on that quantisation noise, a mean-|x| floor does.) */
    if (blocks <= 25) floor_pow += 0.2f * (pow_blk - floor_pow);
    else if (pow_blk < 10.0f * floor_pow) floor_pow += 0.02f * (pow_blk - floor_pow);
    else floor_pow += 0.001f * (pow_blk - floor_pow);
    if (floor_pow < 1e-10f) floor_pow = 1e-10f;
    if (cal_left) {
        int bin = (int)(block_db + 100.0f);
        cal_hist[bin < 0 ? 0 : bin > 99 ? 99 : bin]++;
        if (--cal_left == 0) room_cal_finish();
    }

    if (tap) tap(stereo, n, done_cycles);
}

int ss_audio_onset(ss_onset *o)
{
    if (ev_r == ev_w) return 0;
    *o = evq[ev_r++ % EVQ];
    return 1;
}
