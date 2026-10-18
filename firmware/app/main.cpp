/* Final hackathon app: PLACEHOLDER (the idea is not decided yet).
 *
 * This is the starting point the final demo grows from. It already does the
 * board bring-up every idea needs, with the fixes found during testing:
 *   - SRAM0/SRAM1 powered through the Secure Enclave (the NPU reads zeros otherwise)
 *   - MIPI/LVDS mux + MIPI clocks + CDC200 display, double-buffered RGB888
 *   - ExecuTorch runtime on the Ethos-U85, with MNIST as a stand-in model
 * It shows a "TBD" screen and runs one stand-in inference per second so the
 * NPU path stays tested while the real model and UI are written.
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
#include "assets/mnist_model_data.h"   /* stand-in model: replace with the real one */
#include "assets/model_io.h"
#include "assets/test_digits.h"

#define LCD_W (RTE_PANEL_HACTIVE_TIME)
#define LCD_H (RTE_PANEL_VACTIVE_LINE)
#if RTE_CDC200_PIXEL_FORMAT != 1
#error "drawing code assumes RGB888 (RTE_CDC200_PIXEL_FORMAT 1)"
#endif

/* Scanned frame buffer in SRAM0 (bytes B, G, R) and a back buffer in SRAM1;
 * draw into canvas, then present(). */
static uint8_t fb[LCD_H][LCD_W][3] __attribute__((section(".bss.lcd_frame_buf"), aligned(16)));
static uint8_t back[LCD_H][LCD_W][3] __attribute__((section(".bss.at_sram1"), aligned(16)));
#define canvas back

extern "C" ARM_DRIVER_CDC200 Driver_CDC200;

struct rgb { uint8_t r, g, b; };
static const rgb kBg     = {16, 20, 32};
static const rgb kHeader = {32, 40, 64};
static const rgb kText   = {235, 238, 245};
static const rgb kMuted  = {140, 150, 170};
static const rgb kGood   = {60, 200, 110};
static const rgb kBad    = {235, 80, 70};

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

/* The CDC200 reads RAM directly, so push the finished frame out of the D-cache. */
static void present(void)
{
    memcpy(fb, back, sizeof(fb));
    SCB_CleanDCache();
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

/* ---------- stand-in workload (replace with the real app) ---------- */

/* Runs one MNIST test digit through the NPU; returns true if it was classified right. */
static bool standin_inference(int idx, uint32_t *npu_us)
{
    static int8_t input_q[784];
    int8_t out_q[10];
    for (int i = 0; i < 784; i++) {
        const float x = ((test_digits[idx][i] / 127.0f) - MODEL_NORM_MEAN) / MODEL_NORM_STD;
        int32_t q = (int32_t)lroundf(x / MODEL_IN_SCALE) + MODEL_IN_ZP;
        input_q[i] = (int8_t)(q < -128 ? -128 : (q > 127 ? 127 : q));
    }
    if (executorch_run_inference(input_q, sizeof(input_q), out_q, sizeof(out_q)) != 0) {
        halt("inference");
    }
    *npu_us = executorch_last_execute_cycles() / (SystemCoreClock / 1000000U);
    int pred = 0;
    for (int d = 1; d < 10; d++) if (out_q[d] > out_q[pred]) pred = d;
    return pred == test_digit_label[idx];
}

static void draw_screen(uint32_t npu_us, int correct, int total)
{
    char buf[64];
    fill_rect(0, 0, LCD_W, LCD_H, kBg);
    fill_rect(0, 0, LCD_W, 64, kHeader);
    draw_text(&font_small, 20, 18, "Alif E8 | Ethos-U85 NPU", kText);

    draw_text(&font_small, 20, 300, "Final app: TBD", kText);
    draw_text(&font_small, 20, 340, "Board, display and NPU are up.", kMuted);

    snprintf(buf, sizeof(buf), "stand-in model: %lu us", (unsigned long)npu_us);
    draw_text(&font_small, 20, LCD_H - 70, buf, kText);
    snprintf(buf, sizeof(buf), "self-check %d/%d", correct, total);
    draw_text(&font_small, 20, LCD_H - 36, buf, correct * 10 >= total * 8 ? kGood : kBad);
    present();
}

int main(void)
{
    platform_init();

    if (executorch_init(mnist_int8io_pte, mnist_int8io_pte_len) != 0) {
        draw_text(&font_small, 20, 420, "Model init failed", kBad);
        present();
        halt("executorch_init");
    }
    executorch_set_verbose(0);
    printf("[APP] placeholder up\r\n");

    int correct = 0, total = 0;
    for (int idx = 0;; idx = (idx + 1) % NUM_TEST_DIGITS) {
        uint32_t npu_us;
        correct += standin_inference(idx, &npu_us);
        total++;
        draw_screen(npu_us, correct, total);
        printf("[APP] npu=%lu us self-check %d/%d\r\n", (unsigned long)npu_us, correct, total);
        delay_ms(1000);
    }
}
