/* NPU live demo on the DK-E8 LCD.
 *
 * Cycles through one MNIST test image per class, runs each through the
 * ExecuTorch model on the Ethos-U85, and draws the input, the prediction,
 * a per-class confidence chart and the measured NPU latency on the 480x800 LCD.
 */
#include "RTE_Components.h"
#include CMSIS_device_header

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "Driver_CDC200.h"
#include "board_config.h"
#include "executorch_runner.h"
extern "C" {
#include "se_services_port.h"
}

#include "assets/fonts.h"
#include "assets/mnist_model_data.h"
#include "assets/model_io.h"
#include "assets/test_digits.h"

#define LCD_W (RTE_PANEL_HACTIVE_TIME)
#define LCD_H (RTE_PANEL_VACTIVE_LINE)
#if RTE_CDC200_PIXEL_FORMAT != 1
#error "drawing code assumes RGB888 (RTE_CDC200_PIXEL_FORMAT 1)"
#endif

/* RGB888 frame buffer in SRAM0 that the CDC200 scans (bytes B, G, R), plus a back
 * buffer in SRAM1 that all drawing goes to; present() copies a finished frame over,
 * so the panel never shows a half-drawn screen. */
static uint8_t fb[LCD_H][LCD_W][3] __attribute__((section(".bss.lcd_frame_buf"), aligned(16)));
static uint8_t back[LCD_H][LCD_W][3] __attribute__((section(".bss.at_sram1"), aligned(16)));
#define canvas back

extern "C" ARM_DRIVER_CDC200 Driver_CDC200;

struct rgb { uint8_t r, g, b; };
static const rgb kBg      = {16, 20, 32};
static const rgb kHeader  = {32, 40, 64};
static const rgb kText    = {235, 238, 245};
static const rgb kMuted   = {140, 150, 170};
static const rgb kGood    = {60, 200, 110};
static const rgb kBad     = {235, 80, 70};
static const rgb kBar     = {70, 100, 170};

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

static int draw_text(const font_t *f, int x, int y, const char *s, rgb c)
{
    for (; *s; s++, x += f->w) {
        if (*s < f->first || *s > f->last) continue;
        const uint8_t *g = f->data + (size_t)(*s - f->first) * f->w * f->h;
        for (int j = 0; j < f->h; j++) {
            int py = y + j;
            if (py < 0 || py >= LCD_H) continue;
            for (int i = 0; i < f->w; i++) {
                int px = x + i;
                unsigned a = g[j * f->w + i];
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

/* Copy the finished back buffer to the scanned frame buffer. The CDC200 reads RAM
 * directly, so push it out of the D-cache too. */
static void present(void)
{
    memcpy(fb, back, sizeof(fb));
    SCB_CleanDCache();
}

static void draw_digit_image(const int8_t *px, int x0, int y0, int scale)
{
    for (int r = 0; r < 28; r++) {
        for (int c = 0; c < 28; c++) {
            uint8_t v = (uint8_t)(px[r * 28 + c] * 2);
            fill_rect(x0 + c * scale, y0 + r * scale, scale, scale, rgb{v, v, v});
        }
    }
}

static void draw_frame(int idx, int label, int pred, const float prob[10],
                       uint32_t npu_us, int correct, int total)
{
    char buf[64];
    const rgb verdict = (pred == label) ? kGood : kBad;

    fill_rect(0, 0, LCD_W, LCD_H, kBg);

    /* header */
    fill_rect(0, 0, LCD_W, 64, kHeader);
    draw_text(&font_small, 20, 18, "Alif E8 | Ethos-U85 NPU", kText);

    /* input */
    snprintf(buf, sizeof(buf), "MNIST test #%d  (label %d)", test_digit_index[idx], label);
    draw_text(&font_small, 72, 80, buf, kMuted);
    const int scale = 12, img = 28 * scale, ix = (LCD_W - img) / 2, iy = 118;
    fill_rect(ix - 3, iy - 3, img + 6, img + 6, kMuted);
    draw_digit_image(test_digits[idx], ix, iy, scale);

    /* prediction */
    const int py = iy + img + 16;
    draw_text(&font_small, 40, py + 40, "NPU says", kMuted);
    snprintf(buf, sizeof(buf), "%d", pred);
    draw_text(&font_large, 200, py, buf, verdict);
    snprintf(buf, sizeof(buf), "%d.%d%%", (int)(prob[pred] * 100.0f),
             (int)(prob[pred] * 1000.0f) % 10);
    draw_text(&font_small, 300, py + 40, buf, kText);
    draw_text(&font_small, 300, py + 76, (pred == label) ? "correct" : "wrong", verdict);

    /* per-class confidence */
    const int base = 722, maxh = 82, slot = 44, x0 = 20;
    for (int d = 0; d < 10; d++) {
        int h = (int)(prob[d] * maxh + 0.5f);
        if (h < 2) h = 2;
        fill_rect(x0 + d * slot + 6, base - h, 32, h, d == pred ? verdict : kBar);
        snprintf(buf, sizeof(buf), "%d", d);
        draw_text(&font_small, x0 + d * slot + 15, base + 2, buf, kMuted);
    }

    /* footer */
    snprintf(buf, sizeof(buf), "NPU %lu us   accuracy %d/%d", (unsigned long)npu_us, correct, total);
    draw_text(&font_small, 20, LCD_H - 30, buf, kText);

    present();
}

/* ---------- platform ---------- */

static void delay_ms(uint32_t ms)
{
    DCB->DEMCR |= DCB_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    const uint32_t start = DWT->CYCCNT, n = ms * (SystemCoreClock / 1000U);
    while ((DWT->CYCCNT - start) < n) {
    }
}

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

    se_services_port_init();
    if (SERVICES_clocks_enable_clock(se_services_s_handle, CLKEN_CLK_100M, true, &err) ||
        SERVICES_clocks_enable_clock(se_services_s_handle, CLKEN_HFOSC, true, &err)) {
        halt("MIPI clocks");
    }
    /* SRAM0 (frame buffer, ExecuTorch pools), SRAM1 (planned memory), MIPI PHYs */
    if (SERVICES_get_run_cfg(se_services_s_handle, &runp, &err)) {
        halt("get_run_cfg");
    }
    runp.memory_blocks  |= MRAM_MASK | SRAM0_MASK | SRAM1_MASK;
    runp.phy_pwr_gating |= MIPI_PLL_DPHY_MASK | MIPI_TX_DPHY_MASK | MIPI_RX_DPHY_MASK | LDO_PHY_MASK;
    if (SERVICES_set_run_cfg(se_services_s_handle, &runp, &err)) {
        halt("set_run_cfg");
    }

    fill_rect(0, 0, LCD_W, LCD_H, kBg);
    draw_text(&font_small, 20, 380, "Loading model on Ethos-U85...", kText);
    present();

    ARM_DRIVER_CDC200 *cdc = &Driver_CDC200;
    if (cdc->Initialize(display_callback) != ARM_DRIVER_OK ||
        cdc->PowerControl(ARM_POWER_FULL) != ARM_DRIVER_OK ||
        cdc->Control(CDC200_CONFIGURE_DISPLAY, (uint32_t)fb) != ARM_DRIVER_OK ||
        cdc->Start() != ARM_DRIVER_OK) {
        halt("display start");
    }
}

int main(void)
{
    platform_init();

    if (executorch_init(mnist_int8io_pte, mnist_int8io_pte_len) != 0) {
        draw_text(&font_small, 20, 420, "Model init failed", kBad);
        present();
        halt("executorch_init");
    }

    static int8_t input_q[784];
    int8_t out_q[10];
    float prob[10];
    int correct = 0, total = 0;

    for (int idx = 0;; idx = (idx + 1) % NUM_TEST_DIGITS) {
        /* pixel (0..127) -> normalized -> int8 model input */
        const int8_t *px = test_digits[idx];
        for (int i = 0; i < 784; i++) {
            const float x = ((px[i] / 127.0f) - MODEL_NORM_MEAN) / MODEL_NORM_STD;
            int32_t q = (int32_t)lroundf(x / MODEL_IN_SCALE) + MODEL_IN_ZP;
            input_q[i] = (int8_t)(q < -128 ? -128 : (q > 127 ? 127 : q));
        }

        if (executorch_run_inference(input_q, sizeof(input_q), out_q, sizeof(out_q)) != 0) {
            halt("inference");
        }
        const uint32_t cycles = executorch_last_execute_cycles();
        const uint32_t npu_us = cycles / (SystemCoreClock / 1000000U);

        /* int8 logits -> softmax */
        int pred = 0;
        float maxl = -1e9f, sum = 0.0f;
        for (int d = 0; d < 10; d++) {
            prob[d] = (out_q[d] - MODEL_OUT_ZP) * MODEL_OUT_SCALE;
            if (prob[d] > maxl) { maxl = prob[d]; pred = d; }
        }
        for (int d = 0; d < 10; d++) { prob[d] = expf(prob[d] - maxl); sum += prob[d]; }
        for (int d = 0; d < 10; d++) prob[d] /= sum;

        const int label = test_digit_label[idx];
        if (total < NUM_TEST_DIGITS) {
            total++;
            correct += (pred == label);
        }
        draw_frame(idx, label, pred, prob, npu_us, correct, total);
        printf("[DEMO] test #%d label=%d pred=%d p=%d%% npu=%lu us (%lu cycles)\r\n",
               test_digit_index[idx], label, pred, (int)(prob[pred] * 100.0f),
               (unsigned long)npu_us, (unsigned long)cycles);

        delay_ms(2500);
    }
}
