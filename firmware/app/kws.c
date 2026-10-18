/* Keyword spotting front end: PDM capture at 16 kHz and log-mel features (see kws.h).
 * Plain C because Driver_PDM.h doesn't compile as C++. */
#include "kws.h"

#include <math.h>
#include <string.h>

#include "Driver_PDM.h"
#include "RTE_Components.h"
#include CMSIS_device_header
#include "se_services_port.h"

extern ARM_DRIVER_PDM Driver_PDM;

/* ---- feature pipeline (mirrors model/kws_common.py Features) ---- */

#define NBINS (KWS_WIN / 2 + 1)

static float hann[KWS_WIN];
static float tw_cos[KWS_WIN / 2], tw_sin[KWS_WIN / 2];
static uint16_t bitrev[KWS_WIN];
static bool tables_ready;

static void tables_init(void)
{
    if (tables_ready) return;
    const float pi = 3.14159265358979f;
    for (int n = 0; n < KWS_WIN; n++) hann[n] = 0.5f - 0.5f * cosf(2.0f * pi * n / KWS_WIN);
    for (int k = 0; k < KWS_WIN / 2; k++) {
        tw_cos[k] = cosf(2.0f * pi * k / KWS_WIN);
        tw_sin[k] = -sinf(2.0f * pi * k / KWS_WIN);
    }
    int bits = 0;
    while ((1 << bits) < KWS_WIN) bits++;
    for (int i = 0; i < KWS_WIN; i++) {
        int r = 0;
        for (int b = 0; b < bits; b++) r |= ((i >> b) & 1) << (bits - 1 - b);
        bitrev[i] = (uint16_t)r;
    }
    tables_ready = true;
}

/* In-place iterative radix-2 complex FFT of KWS_WIN points. */
static void fft(float *re, float *im)
{
    for (int i = 0; i < KWS_WIN; i++) {
        int j = bitrev[i];
        if (j > i) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int len = 2; len <= KWS_WIN; len <<= 1) {
        int half = len / 2, step = KWS_WIN / len;
        for (int s = 0; s < KWS_WIN; s += len) {
            for (int k = 0; k < half; k++) {
                float wr = tw_cos[k * step], wi = tw_sin[k * step];
                int a = s + k, b = a + half;
                float xr = re[b] * wr - im[b] * wi;
                float xi = re[b] * wi + im[b] * wr;
                re[b] = re[a] - xr; im[b] = im[a] - xi;
                re[a] += xr;        im[a] += xi;
            }
        }
    }
}

/* One frame of KWS_WIN samples (already scaled to [-1, 1)) -> KWS_NMEL log-mel values. */
static void frame_logmel(const float *x, float *out)
{
    static float re[KWS_WIN], im[KWS_WIN], power[NBINS];
    for (int n = 0; n < KWS_WIN; n++) { re[n] = x[n] * hann[n]; im[n] = 0.0f; }
    fft(re, im);
    for (int k = 0; k < NBINS; k++) power[k] = re[k] * re[k] + im[k] * im[k];
    const float *w = kws_mel_w;
    for (int b = 0; b < KWS_NMEL; b++) {
        float acc = 0.0f;
        for (int i = 0; i < kws_mel_len[b]; i++) acc += power[kws_mel_first[b] + i] * w[i];
        w += kws_mel_len[b];
        out[b] = logf(acc + KWS_LOG_EPS);
    }
}

static void quantize_patch(float (*mel)[KWS_NMEL], int first, int8_t *out, float in_scale, int in_zp)
{
    /* mel is a ring of KWS_FRAMES rows; 'first' is the oldest */
    float mean = 0.0f;
    for (int f = 0; f < KWS_FRAMES; f++)
        for (int b = 0; b < KWS_NMEL; b++) mean += mel[f][b];
    mean /= (float)(KWS_FRAMES * KWS_NMEL);
    const float k = 1.0f / (KWS_FEAT_STD * in_scale);
    for (int f = 0; f < KWS_FRAMES; f++) {
        const float *row = mel[(first + f) % KWS_FRAMES];
        for (int b = 0; b < KWS_NMEL; b++) {
            int q = (int)lrintf((row[b] - mean) * k) + in_zp;
            out[f * KWS_NMEL + b] = (int8_t)(q < -128 ? -128 : q > 127 ? 127 : q);
        }
    }
}

void kws_features_of_clip(const int16_t *clip, int8_t *out, float in_scale, int in_zp)
{
    static float mel[KWS_FRAMES][KWS_NMEL], frame[KWS_WIN];
    tables_init();
    for (int f = 0; f < KWS_FRAMES; f++) {
        for (int n = 0; n < KWS_WIN; n++) frame[n] = clip[f * KWS_HOP + n] / 32768.0f;
        frame_logmel(frame, mel[f]);
    }
    quantize_patch(mel, 0, out, in_scale, in_zp);
}

/* ---- streaming capture ---- */

/* PDM channels 4 and 5 (PDM_D2) interleave 16-bit samples: 2 per time step. Blocks are one
 * hop (20 ms); the driver fills one block while the main loop reads the other. */
#define NBLK 4
static int16_t pcm[NBLK][2 * KWS_HOP];
static volatile uint32_t blk_written, blk_read, dropped;
static volatile bool pdm_error;

static float window_buf[KWS_WIN];             /* last KWS_WIN mono samples */
static float mel_ring[KWS_FRAMES][KWS_NMEL];
static int mel_next;                          /* next row to write = oldest row */
static float level_db = -120.0f;

/* Same filter set as hw_test (from Alif's demo_pdm.c); gain explained there. */
static const uint32_t ch4_fir[18] = {0x00000001, 0x00000003, 0x00000003, 0x000007F4, 0x00000004, 0x000007ED,
                                     0x000007F5, 0x000007F4, 0x000007D3, 0x000007FE, 0x000007BC, 0x000007E5,
                                     0x000007D9, 0x00000793, 0x00000029, 0x0000072C, 0x00000072, 0x000002FD};
static const uint32_t ch5_fir[18] = {0x00000000, 0x000007FF, 0x00000000, 0x00000004, 0x00000004, 0x000007FC,
                                     0x00000000, 0x000007FB, 0x000007E4, 0x00000000, 0x0000002B, 0x00000009,
                                     0x00000016, 0x00000049, 0x00000793, 0x000006F8, 0x00000045, 0x00000178};
#define MIC_GAIN_CH4 0x800
#define MIC_GAIN_CH5 0xBB0

static void pdm_cb(uint32_t event)
{
    if (event & ARM_PDM_EVENT_ERROR) { pdm_error = true; return; }
    if (!(event & ARM_PDM_EVENT_CAPTURE_COMPLETE)) return;
    blk_written++;
    if (blk_written - blk_read >= NBLK) { blk_read++; dropped++; }  /* overwrite the oldest */
    Driver_PDM.Receive(pcm[blk_written % NBLK], 2 * KWS_HOP);
}

int kws_audio_start(void)
{
    uint32_t err;
    ARM_DRIVER_PDM *p = &Driver_PDM;
    PDM_CH_CONFIG cfg;
    tables_init();
    if (SERVICES_clocks_enable_clock(se_services_s_handle, CLKEN_HFOSCx2, true, &err) ||
        p->Initialize(pdm_cb) || p->PowerControl(ARM_POWER_FULL) ||
        p->Control(ARM_PDM_SELECT_CHANNEL, ARM_PDM_MASK_CHANNEL_4 | ARM_PDM_MASK_CHANNEL_5, 0) ||
        p->Control(ARM_PDM_MODE, ARM_PDM_MODE_AUDIOFREQ_16K_DECM_64, 0) ||
        p->Control(ARM_PDM_BYPASS_IIR_FILTER, 0, 0) ||  /* keep the DC-blocking filter */
        p->Control(ARM_PDM_CHANNEL_PHASE, 4, 0x1F) || p->Control(ARM_PDM_CHANNEL_GAIN, 4, MIC_GAIN_CH4) ||
        p->Control(ARM_PDM_CHANNEL_PEAK_DETECT_TH, 4, 0x00060002) ||
        p->Control(ARM_PDM_CHANNEL_PEAK_DETECT_ITV, 4, 0x0004002D) ||
        p->Control(ARM_PDM_CHANNEL_PHASE, 5, 0x03) || p->Control(ARM_PDM_CHANNEL_GAIN, 5, MIC_GAIN_CH5) ||
        p->Control(ARM_PDM_CHANNEL_PEAK_DETECT_TH, 5, 0x00060002) ||
        p->Control(ARM_PDM_CHANNEL_PEAK_DETECT_ITV, 5, 0x00020027))
        return -1;
    cfg.ch_num = 4;
    memcpy(cfg.ch_fir_coef, ch4_fir, sizeof(cfg.ch_fir_coef));
    cfg.ch_iir_coef = 0x00000004;
    if (p->Config(&cfg)) return -2;
    cfg.ch_num = 5;
    memcpy(cfg.ch_fir_coef, ch5_fir, sizeof(cfg.ch_fir_coef));
    cfg.ch_iir_coef = 0x00000004;
    if (p->Config(&cfg)) return -3;
    for (int f = 0; f < KWS_FRAMES; f++)
        for (int b = 0; b < KWS_NMEL; b++) mel_ring[f][b] = logf(KWS_LOG_EPS);
    if (p->Receive(pcm[0], 2 * KWS_HOP) != ARM_DRIVER_OK) return -4;
    return 0;
}

int kws_audio_poll(void)
{
    int frames = 0;
    while (blk_read != blk_written) {
        const int16_t *blk = pcm[blk_read % NBLK];
        /* slide the analysis window by one hop and append the new mono samples (mic 4) */
        memmove(window_buf, window_buf + KWS_HOP, (KWS_WIN - KWS_HOP) * sizeof(float));
        float *dst = window_buf + (KWS_WIN - KWS_HOP);
        float acc = 0.0f;
        for (int i = 0; i < KWS_HOP; i++) {
            float s = blk[2 * i] / 32768.0f;
            dst[i] = s;
            acc += s * s;
        }
        blk_read++;
        level_db = acc > 0.0f ? 10.0f * log10f(acc / KWS_HOP) : -120.0f;
        frame_logmel(window_buf, mel_ring[mel_next]);
        mel_next = (mel_next + 1) % KWS_FRAMES;
        frames++;
    }
    return frames;
}

float kws_audio_level_db(void) { return level_db; }
uint32_t kws_audio_dropped(void) { return dropped; }

void kws_make_input(int8_t *out, float in_scale, int in_zp)
{
    quantize_patch(mel_ring, mel_next, out, in_scale, in_zp);
}
