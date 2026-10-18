/* Final hackathon app: voice + motion controller. Stage 1: live keyword spotting.
 *
 * The two PDM mics stream 16 kHz audio (interrupt driven, kws.c). Every 20 ms the M55
 * turns the newest 32 ms window into one 40-band log-mel frame. Every 100 ms the latest
 * 49 frames (about 1 s) go to a DS-CNN on the Ethos-U85 (ExecuTorch, int8). The scores
 * of the last 3 runs are averaged, and a keyword is reported when it leads with > 70%.
 *
 * At boot, a self-test runs the C feature code on a Speech Commands clip and compares the
 * result with what Python computed (kws_testclip.h, from model/export_kws.py).
 *
 * Bring-up fixes this relies on (see docs/bringup-notes.md): SRAM0/1 powered through the
 * Secure Enclave, the MIPI/LVDS mux set by board_gpios_config(), cache hooks and address
 * remap in executorch_runner.cpp, int8 model IO.
 */
#include "RTE_Components.h"
#include CMSIS_device_header

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Driver_CDC200.h"
#include "board_config.h"
#include "executorch_runner.h"
#include "kws.h"
extern "C" {
#include "se_services_port.h"
}

#include "assets/fonts.h"
#include "assets/kws_io.h"
#include "assets/kws_model_data.h"
#include "kws_testclip.h"

#define LCD_W (RTE_PANEL_HACTIVE_TIME)
#define LCD_H (RTE_PANEL_VACTIVE_LINE)
#if RTE_CDC200_PIXEL_FORMAT != 1
#error "drawing code assumes RGB888 (RTE_CDC200_PIXEL_FORMAT 1)"
#endif

#define KWS_INPUT_LEN   (KWS_FRAMES * KWS_NMEL)
#define INFER_EVERY     5        /* frames (20 ms each) between inferences */
#define SMOOTH_RUNS     3        /* runs averaged for the decision */
#define DETECT_PROB     0.70f
#define REFRACTORY_MS   1000

/* Scanned frame buffer in SRAM0 (bytes B, G, R) and a back buffer in SRAM1;
 * draw into canvas, then present(). */
static uint8_t fb[LCD_H][LCD_W][3] __attribute__((section(".bss.lcd_frame_buf"), aligned(16)));
static uint8_t back[LCD_H][LCD_W][3] __attribute__((section(".bss.at_sram1"), aligned(16)));
#define canvas back

extern "C" ARM_DRIVER_CDC200 Driver_CDC200;

struct rgb { uint8_t r, g, b; };
static const rgb kBg     = {16, 20, 32};
static const rgb kHeader = {32, 40, 64};
static const rgb kPanel  = {28, 34, 52};
static const rgb kText   = {235, 238, 245};
static const rgb kMuted  = {140, 150, 170};
static const rgb kGood   = {60, 200, 110};
static const rgb kBad    = {235, 80, 70};
static const rgb kAccent = {90, 160, 255};

/* ---------- drawing ---------- */

static void fill_rect(int x, int y, int w, int h, rgb c)
{
    for (int j = y; j < y + h && j < LCD_H; j++) {
        if (j < 0) continue;
        for (int i = x; i < x + w && i < LCD_W; i++) {
            if (i < 0) continue;
            canvas[j][i][0] = c.b;
            canvas[j][i][1] = c.g;
            canvas[j][i][2] = c.r;
        }
    }
}

/* Text with nearest-neighbour scaling (scale 1 = the font's own size). */
static int draw_text(const font_t *f, int x, int y, const char *s, rgb c, int scale = 1)
{
    for (; *s; s++, x += f->w * scale) {
        if (*s < f->first || *s > f->last) continue;
        const uint8_t *g = f->data + (size_t)(*s - f->first) * f->w * f->h;
        for (int j = 0; j < f->h * scale; j++) {
            int py = y + j;
            if (py < 0 || py >= LCD_H) continue;
            for (int i = 0; i < f->w * scale; i++) {
                int px = x + i;
                unsigned a = g[(j / scale) * f->w + i / scale];
                if (!a || px < 0 || px >= LCD_W) continue;
                uint8_t *d = canvas[py][px];
                d[0] = (uint8_t)(d[0] + ((int)c.b - d[0]) * (int)a / 255);
                d[1] = (uint8_t)(d[1] + ((int)c.g - d[1]) * (int)a / 255);
                d[2] = (uint8_t)(d[2] + ((int)c.r - d[2]) * (int)a / 255);
            }
        }
    }
    return x;
}

static void hbar(int x, int y, int w, int h, float frac, rgb c)
{
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    fill_rect(x, y, w, h, kPanel);
    fill_rect(x, y, (int)(w * frac), h, c);
}

/* The CDC200 reads RAM directly, so push the finished frame out of the D-cache. */
static void present(void)
{
    memcpy(fb, back, sizeof(fb));
    SCB_CleanDCache();
}

/* ---------- platform ---------- */

static uint32_t now_ms(void)
{
    /* DWT counter wraps every ~10.7 s at 400 MHz, so extend it in software */
    static uint32_t last, ms, rem;
    const uint32_t c = DWT->CYCCNT, per_ms = SystemCoreClock / 1000U;
    rem += c - last;
    last = c;
    ms += rem / per_ms;
    rem %= per_ms;
    return ms;
}

static uint32_t cycles_to_us(uint32_t c) { return c / (SystemCoreClock / 1000000U); }

static void halt(const char *why)
{
    printf("ERROR: %s\r\n", why);
    while (1) __WFI();
}

static void display_callback(uint32_t event)
{
    if (event & ARM_CDC_DSI_ERROR_EVENT) {
        halt("DSI error");
    }
}

static void platform_init(void)
{
    uint32_t err;
    run_profile_t runp = {0};

    if (board_pins_config() != 0 || board_gpios_config() != 0) {
        halt("board pin/gpio config");   /* gpios also selects the MIPI/LVDS mux */
    }
    DCB->DEMCR |= DCB_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    se_services_port_init();
    if (SERVICES_clocks_enable_clock(se_services_s_handle, CLKEN_CLK_100M, true, &err) ||
        SERVICES_clocks_enable_clock(se_services_s_handle, CLKEN_HFOSC, true, &err)) {
        halt("MIPI clocks");
    }
    /* SRAM0 (frame buffer, ExecuTorch pools), SRAM1 (back buffer), MIPI PHYs */
    if (SERVICES_get_run_cfg(se_services_s_handle, &runp, &err)) {
        halt("get_run_cfg");
    }
    runp.memory_blocks  |= MRAM_MASK | SRAM0_MASK | SRAM1_MASK;
    runp.phy_pwr_gating |= MIPI_PLL_DPHY_MASK | MIPI_TX_DPHY_MASK | MIPI_RX_DPHY_MASK | LDO_PHY_MASK;
    if (SERVICES_set_run_cfg(se_services_s_handle, &runp, &err)) {
        halt("set_run_cfg");
    }

    fill_rect(0, 0, LCD_W, LCD_H, kBg);
    draw_text(&font_small, 20, 380, "Starting...", kText);
    present();

    ARM_DRIVER_CDC200 *cdc = &Driver_CDC200;
    if (cdc->Initialize(display_callback) != ARM_DRIVER_OK ||
        cdc->PowerControl(ARM_POWER_FULL) != ARM_DRIVER_OK ||
        cdc->Control(CDC200_CONFIGURE_DISPLAY, (uint32_t)fb) != ARM_DRIVER_OK ||
        cdc->Start() != ARM_DRIVER_OK) {
        halt("display start");
    }
}

/* ---------- keyword spotting ---------- */

struct kws_result {
    float prob[KWS_NUM_LABELS];
    uint32_t feat_us, npu_us, total_us;   /* features, NPU execute, whole runner call */
};

static int argmax(const float *p, int n)
{
    int best = 0;
    for (int i = 1; i < n; i++) if (p[i] > p[best]) best = i;
    return best;
}

/* Runs the model on a quantized input; fills probabilities (softmax of the int8 logits). */
static bool kws_infer(const int8_t *input, int8_t *out_q, kws_result *r)
{
    const uint32_t t0 = DWT->CYCCNT;
    if (executorch_run_inference(input, KWS_INPUT_LEN, out_q, KWS_NUM_LABELS) != 0) return false;
    r->total_us = cycles_to_us(DWT->CYCCNT - t0);
    r->npu_us = cycles_to_us(executorch_last_execute_cycles());
    float logit[KWS_NUM_LABELS], mx = -1e9f, sum = 0.0f;
    for (int i = 0; i < KWS_NUM_LABELS; i++) {
        logit[i] = (out_q[i] - KWS_OUT_ZP) * KWS_OUT_SCALE;
        if (logit[i] > mx) mx = logit[i];
    }
    for (int i = 0; i < KWS_NUM_LABELS; i++) sum += (r->prob[i] = expf(logit[i] - mx));
    for (int i = 0; i < KWS_NUM_LABELS; i++) r->prob[i] /= sum;
    return true;
}

/* Boot self-test: C features vs Python on a known clip, then the NPU output vs the host. */
static char selftest_msg[64];
static bool selftest_ok;

static void self_test(void)
{
    static int8_t in_q[KWS_INPUT_LEN];
    int8_t out_q[KWS_NUM_LABELS];
    kws_result r;
    const uint32_t t0 = DWT->CYCCNT;
    kws_features_of_clip(kws_test_clip, in_q, KWS_IN_SCALE, KWS_IN_ZP);
    const uint32_t feat_us = cycles_to_us(DWT->CYCCNT - t0);
    int max_in = 0, off_in = 0;
    for (int i = 0; i < KWS_INPUT_LEN; i++) {
        int d = abs(in_q[i] - kws_test_input[i]);
        if (d > max_in) max_in = d;
        off_in += d > 0;
    }
    if (!kws_infer(in_q, out_q, &r)) halt("self-test inference");
    int max_out = 0;
    for (int i = 0; i < KWS_NUM_LABELS; i++) {
        int d = abs(out_q[i] - kws_test_output[i]);
        if (d > max_out) max_out = d;
    }
    const int pred = argmax(r.prob, KWS_NUM_LABELS);
    selftest_ok = pred == KWS_TEST_LABEL && max_in <= 1;
    printf("[KWS] self-test: features %lu us, %d/%d inputs differ from Python (max %d LSB); "
           "output max diff %d LSB; predicted '%s' (%d%%), expected '%s'; npu %lu us, runner %lu us\r\n",
           (unsigned long)feat_us, off_in, KWS_INPUT_LEN, max_in, max_out, kws_labels[pred],
           (int)(r.prob[pred] * 100), kws_labels[KWS_TEST_LABEL], (unsigned long)r.npu_us,
           (unsigned long)r.total_us);
    snprintf(selftest_msg, sizeof(selftest_msg), "self-test %s: '%s', feat max diff %d",
             selftest_ok ? "OK" : "FAIL", kws_labels[pred], max_in);
}

/* ---------- screen ---------- */

#define WORD_Y   90
#define BARS_Y   300
#define ROW_H    30
#define STATS_Y  (BARS_Y + KWS_NUM_LABELS * ROW_H + 16)

static void draw_static(void)
{
    fill_rect(0, 0, LCD_W, LCD_H, kBg);
    fill_rect(0, 0, LCD_W, 64, kHeader);
    draw_text(&font_small, 20, 18, "Alif E8 | voice on Ethos-U85", kText);
    draw_text(&font_small, 20, 250, "say: yes no up down left right", kMuted);
    draw_text(&font_small, 20, 272, "     on off stop go", kMuted);
}

static void draw_dynamic(const char *word, rgb word_col, const float *avg, float level_db,
                         const kws_result &r, uint32_t runs, uint32_t detections)
{
    char buf[64];
    /* detected word, 3x font, centred */
    fill_rect(0, WORD_Y, LCD_W, 3 * font_small.h + 20, kBg);
    const int ww = (int)strlen(word) * font_small.w * 3;
    draw_text(&font_small, (LCD_W - ww) / 2, WORD_Y + 10, word, word_col, 3);

    /* per-class bars of the smoothed probabilities */
    const int top = argmax(avg, KWS_NUM_LABELS);
    for (int i = 0; i < KWS_NUM_LABELS; i++) {
        const int y = BARS_Y + i * ROW_H;
        fill_rect(0, y, LCD_W, ROW_H, kBg);
        draw_text(&font_small, 20, y, kws_labels[i], i == top ? kText : kMuted);
        hbar(170, y + 6, 280, ROW_H - 12, avg[i], i == top ? kAccent : kMuted);
    }

    /* mic level and timing */
    fill_rect(0, STATS_Y, LCD_W, LCD_H - STATS_Y, kBg);
    snprintf(buf, sizeof(buf), "mic %d dBFS", (int)level_db);
    draw_text(&font_small, 20, STATS_Y, buf, kText);
    hbar(200, STATS_Y + 8, 250, 14, (level_db + 80.0f) / 80.0f, level_db > -6 ? kBad : kGood);
    snprintf(buf, sizeof(buf), "NPU %lu us  runner %lu us", (unsigned long)r.npu_us,
             (unsigned long)r.total_us);
    draw_text(&font_small, 20, STATS_Y + 32, buf, kText);
    snprintf(buf, sizeof(buf), "features %lu us  runs %lu  hits %lu", (unsigned long)r.feat_us,
             (unsigned long)runs, (unsigned long)detections);
    draw_text(&font_small, 20, STATS_Y + 64, buf, kMuted);
    draw_text(&font_small, 20, STATS_Y + 96, selftest_msg, selftest_ok ? kGood : kBad);
    present();
}

int main(void)
{
    platform_init();

    if (executorch_init(kws_ethos_u85_pte, kws_ethos_u85_pte_len) != 0) {
        draw_text(&font_small, 20, 420, "Model init failed", kBad);
        present();
        halt("executorch_init");
    }
    executorch_set_verbose(0);
    self_test();

    draw_static();
    if (kws_audio_start() != 0) {
        draw_text(&font_small, 20, 420, "Microphone start failed", kBad);
        present();
        halt("kws_audio_start");
    }
    printf("[KWS] listening\r\n");

    static int8_t in_q[KWS_INPUT_LEN];
    int8_t out_q[KWS_NUM_LABELS];
    float hist[SMOOTH_RUNS][KWS_NUM_LABELS] = {};
    float avg[KWS_NUM_LABELS] = {};
    kws_result r = {};
    int frames = 0, last_word = -1;
    uint32_t runs = 0, detections = 0, last_hit_ms = 0, last_log_ms = 0;
    const char *shown = "listening";
    rgb shown_col = kMuted;

    for (;;) {
        frames += kws_audio_poll();
        if (frames < INFER_EVERY) continue;
        frames = 0;

        uint32_t t0 = DWT->CYCCNT;
        kws_make_input(in_q, KWS_IN_SCALE, KWS_IN_ZP);
        r.feat_us = cycles_to_us(DWT->CYCCNT - t0);
        if (!kws_infer(in_q, out_q, &r)) halt("inference");
        memcpy(hist[runs % SMOOTH_RUNS], r.prob, sizeof(r.prob));
        runs++;
        for (int i = 0; i < KWS_NUM_LABELS; i++) {
            avg[i] = 0;
            for (int k = 0; k < SMOOTH_RUNS; k++) avg[i] += hist[k][i];
            avg[i] /= SMOOTH_RUNS;
        }

        const uint32_t t = now_ms();
        const int top = argmax(avg, KWS_NUM_LABELS);
        if (top >= 2 && avg[top] > DETECT_PROB &&
            (top != last_word || t - last_hit_ms > REFRACTORY_MS)) {
            detections++;
            last_word = top;
            last_hit_ms = t;
            shown = kws_labels[top];
            shown_col = kGood;
            printf("[KWS] t=%lu ms '%s' %d%%\r\n", (unsigned long)t, kws_labels[top], (int)(avg[top] * 100));
        } else if (t - last_hit_ms > 1500 && shown_col.g == kGood.g) {
            shown_col = kMuted;   /* fade the last word after 1.5 s */
        }

        draw_dynamic(shown, shown_col, avg, kws_audio_level_db(), r, runs, detections);

        if (t - last_log_ms > 2000) {
            last_log_ms = t;
            printf("[KWS] t=%lu ms level %d dBFS top '%s' %d%% npu %lu us runner %lu us feat %lu us "
                   "dropped %lu\r\n",
                   (unsigned long)t, (int)kws_audio_level_db(), kws_labels[top], (int)(avg[top] * 100),
                   (unsigned long)r.npu_us, (unsigned long)r.total_us, (unsigned long)r.feat_us,
                   (unsigned long)kws_audio_dropped());
        }
    }
}
