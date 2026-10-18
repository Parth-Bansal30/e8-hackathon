/* Draw a digit on the DK-E8 touchscreen; the Ethos-U85 recognizes it live.
 *
 * Touch (GT911 over I2C1) -> 140x140 drawing pad -> MNIST-style preprocessing
 * (crop, scale to 20 px, center of mass at 14,14) -> int8 model on the NPU ->
 * prediction, confidence bars and timings drawn on the 480x800 MIPI LCD.
 *
 * Tap CLEAR, or start a new digit after a 1.5 s pause, to clear the pad.
 */
#include "RTE_Components.h"
#include CMSIS_device_header

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "Driver_CDC200.h"
#include "Driver_Touch_Screen.h"
#include "board_config.h"
#include "executorch_runner.h"
extern "C" {
#include "se_services_port.h"
}

#include "assets/fonts.h"
#include "assets/mnist_model_data.h"
#include "assets/model_io.h"

/* Touch orientation, if strokes appear mirrored or rotated relative to the finger. */
#ifndef TOUCH_SWAP_XY
#define TOUCH_SWAP_XY 0
#endif
#ifndef TOUCH_FLIP_X
#define TOUCH_FLIP_X 0
#endif
#ifndef TOUCH_FLIP_Y
#define TOUCH_FLIP_Y 0
#endif

#define LCD_W (RTE_PANEL_HACTIVE_TIME)
#define LCD_H (RTE_PANEL_VACTIVE_LINE)
#if RTE_CDC200_PIXEL_FORMAT != 1
#error "drawing code assumes RGB888 (RTE_CDC200_PIXEL_FORMAT 1)"
#endif

/* Scanned frame buffer (SRAM0, bytes B,G,R) and the back buffer all drawing goes to. */
static uint8_t fb[LCD_H][LCD_W][3] __attribute__((section(".bss.lcd_frame_buf"), aligned(16)));
static uint8_t back[LCD_H][LCD_W][3] __attribute__((section(".bss.at_sram1"), aligned(16)));

extern "C" ARM_DRIVER_CDC200 Driver_CDC200;
extern "C" ARM_DRIVER_TOUCH_SCREEN GT911;

struct rgb { uint8_t r, g, b; };
static const rgb kBg     = {16, 20, 32};
static const rgb kHeader = {32, 40, 64};
static const rgb kButton = {70, 90, 140};
static const rgb kText   = {235, 238, 245};
static const rgb kMuted  = {140, 150, 170};
static const rgb kGood   = {60, 200, 110};
static const rgb kBar    = {70, 100, 170};
static const rgb kCursor = {250, 210, 60};

/* ---------- time ---------- */

static volatile uint32_t g_ms;
extern "C" void SysTick_Handler(void) { g_ms++; }
static uint32_t now_ms(void) { return g_ms; }

/* ---------- drawing primitives (into the back buffer) ---------- */

static void fill_rect(int x, int y, int w, int h, rgb c)
{
    for (int j = (y < 0 ? 0 : y); j < y + h && j < LCD_H; j++) {
        for (int i = (x < 0 ? 0 : x); i < x + w && i < LCD_W; i++) {
            back[j][i][0] = c.b;
            back[j][i][1] = c.g;
            back[j][i][2] = c.r;
        }
    }
}

static void draw_text(const font_t *f, int x, int y, const char *s, rgb c)
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
                uint8_t *d = back[py][px];
                d[0] = (uint8_t)(d[0] + ((int)c.b - d[0]) * (int)a / 255);
                d[1] = (uint8_t)(d[1] + ((int)c.g - d[1]) * (int)a / 255);
                d[2] = (uint8_t)(d[2] + ((int)c.r - d[2]) * (int)a / 255);
            }
        }
    }
}

static void present(void)
{
    memcpy(fb, back, sizeof(fb));
    SCB_CleanDCache();   /* the CDC200 reads RAM directly */
}

/* ---------- drawing pad ---------- */

#define PAD_N     140                  /* pad cells per side */
#define PAD_SCALE 3                    /* screen pixels per cell */
#define PAD_PX    (PAD_N * PAD_SCALE)  /* 420 */
#define PAD_X     30
#define PAD_Y     80
#define BRUSH_R   5                    /* cells; ~2 px stroke after scaling to MNIST */

static uint8_t pad[PAD_N][PAD_N];
static bool pad_empty = true;

static void pad_clear(void)
{
    memset(pad, 0, sizeof(pad));
    pad_empty = true;
}

static void pad_stamp(int cx, int cy)
{
    for (int dy = -BRUSH_R - 1; dy <= BRUSH_R + 1; dy++) {
        for (int dx = -BRUSH_R - 1; dx <= BRUSH_R + 1; dx++) {
            int x = cx + dx, y = cy + dy;
            if (x < 0 || y < 0 || x >= PAD_N || y >= PAD_N) continue;
            /* soft edge: full ink inside the radius, fading over one cell */
            float d = sqrtf((float)(dx * dx + dy * dy)) - BRUSH_R;
            int v = d <= 0.0f ? 255 : (d >= 1.0f ? 0 : (int)(255 * (1.0f - d)));
            if (v > pad[y][x]) pad[y][x] = (uint8_t)v;
        }
    }
    pad_empty = false;
}

static void pad_line(int x0, int y0, int x1, int y1)
{
    int dx = x1 - x0, dy = y1 - y0;
    int n = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
    if (n == 0) {
        pad_stamp(x0, y0);
        return;
    }
    for (int i = 0; i <= n; i++) {
        pad_stamp(x0 + dx * i / n, y0 + dy * i / n);
    }
}

/* MNIST preprocessing: crop to the ink, scale the longest side to 20 px with area
 * averaging, then place it so its center of mass sits at (14, 14) in a 28x28 frame.
 * Returns false if the pad is empty. */
static bool preprocess(uint8_t img28[28][28])
{
    int x0 = PAD_N, y0 = PAD_N, x1 = -1, y1 = -1;
    for (int y = 0; y < PAD_N; y++) {
        for (int x = 0; x < PAD_N; x++) {
            if (pad[y][x]) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
        }
    }
    if (x1 < 0) return false;

    const int bw = x1 - x0 + 1, bh = y1 - y0 + 1, side = bw > bh ? bw : bh;
    const int ow = (bw * 20 + side - 1) / side, oh = (bh * 20 + side - 1) / side;
    static float small[20][20];
    float mass = 0.0f, mx = 0.0f, my = 0.0f;
    for (int oy = 0; oy < oh; oy++) {
        for (int ox = 0; ox < ow; ox++) {
            int sx0 = x0 + ox * side / 20, sx1 = x0 + (ox + 1) * side / 20;
            int sy0 = y0 + oy * side / 20, sy1 = y0 + (oy + 1) * side / 20;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sy1 <= sy0) sy1 = sy0 + 1;
            float acc = 0.0f;
            for (int sy = sy0; sy < sy1 && sy < PAD_N; sy++) {
                for (int sx = sx0; sx < sx1 && sx < PAD_N; sx++) {
                    acc += pad[sy][sx];
                }
            }
            float v = acc / (float)((sx1 - sx0) * (sy1 - sy0));
            small[oy][ox] = v;
            mass += v;
            mx += v * (ox + 0.5f);
            my += v * (oy + 0.5f);
        }
    }
    const int offx = (int)lroundf(14.0f - mx / mass), offy = (int)lroundf(14.0f - my / mass);

    memset(img28, 0, 28 * 28);
    for (int oy = 0; oy < oh; oy++) {
        for (int ox = 0; ox < ow; ox++) {
            int tx = ox + offx, ty = oy + offy;
            if (tx < 0 || ty < 0 || tx >= 28 || ty >= 28) continue;
            float v = small[oy][ox];
            img28[ty][tx] = (uint8_t)(v > 255.0f ? 255 : v);
        }
    }
    return true;
}

/* ---------- UI ---------- */

#define BTN_X 350
#define BTN_Y 10
#define BTN_W 116
#define BTN_H 44

struct ui_state {
    bool has_pred;
    int pred;
    float prob[10];
    uint8_t img28[28][28];
    uint32_t npu_us;
    uint32_t frame_ms;
    bool cursor;
    int cursor_x, cursor_y;
};

static void render(const ui_state &s)
{
    char buf[64];

    fill_rect(0, 0, LCD_W, LCD_H, kBg);

    /* header + CLEAR button */
    fill_rect(0, 0, LCD_W, 64, kHeader);
    draw_text(&font_small, 20, 18, "Draw a digit", kText);
    fill_rect(BTN_X, BTN_Y, BTN_W, BTN_H, kButton);
    draw_text(&font_small, BTN_X + (BTN_W - 5 * font_small.w) / 2, BTN_Y + 8, "CLEAR", kText);

    /* pad */
    fill_rect(PAD_X - 3, PAD_Y - 3, PAD_PX + 6, PAD_PX + 6, kMuted);
    fill_rect(PAD_X, PAD_Y, PAD_PX, PAD_PX, rgb{0, 0, 0});
    for (int y = 0; y < PAD_N; y++) {
        for (int x = 0; x < PAD_N; x++) {
            uint8_t v = pad[y][x];
            if (v) fill_rect(PAD_X + x * PAD_SCALE, PAD_Y + y * PAD_SCALE, PAD_SCALE, PAD_SCALE, rgb{v, v, v});
        }
    }
    if (pad_empty) {
        const char *hint = "draw 0-9 here";
        draw_text(&font_small, PAD_X + (PAD_PX - (int)strlen(hint) * font_small.w) / 2, PAD_Y + 195, hint, kMuted);
    }
    if (s.cursor) {
        fill_rect(s.cursor_x - 10, s.cursor_y - 1, 21, 3, kCursor);
        fill_rect(s.cursor_x - 1, s.cursor_y - 10, 3, 21, kCursor);
    }

    if (s.has_pred) {
        /* prediction */
        draw_text(&font_small, 24, 530, "NPU says", kMuted);
        snprintf(buf, sizeof(buf), "%d", s.pred);
        draw_text(&font_large, 150, 506, buf, kGood);
        snprintf(buf, sizeof(buf), "%d.%d%%", (int)(s.prob[s.pred] * 100.0f),
                 (int)(s.prob[s.pred] * 1000.0f) % 10);
        draw_text(&font_small, 24, 570, buf, kText);

        /* what the NPU actually sees (28x28 input) */
        const int tx = 340, ty = 512, k = 4;
        fill_rect(tx - 2, ty - 2, 28 * k + 4, 28 * k + 4, kMuted);
        for (int y = 0; y < 28; y++) {
            for (int x = 0; x < 28; x++) {
                uint8_t v = s.img28[y][x];
                fill_rect(tx + x * k, ty + y * k, k, k, rgb{v, v, v});
            }
        }
        draw_text(&font_small, tx - 4, ty + 28 * k + 4, "NPU input", kMuted);

        /* per-class confidence */
        const int base = 736, maxh = 56, slot = 44, x0 = 20;
        for (int d = 0; d < 10; d++) {
            int h = (int)(s.prob[d] * maxh + 0.5f);
            if (h < 2) h = 2;
            fill_rect(x0 + d * slot + 6, base - h, 32, h, d == s.pred ? kGood : kBar);
            snprintf(buf, sizeof(buf), "%d", d);
            draw_text(&font_small, x0 + d * slot + 15, base + 2, buf, kMuted);
        }

        snprintf(buf, sizeof(buf), "NPU %lu us | frame %lu ms",
                 (unsigned long)s.npu_us, (unsigned long)s.frame_ms);
        draw_text(&font_small, 20, LCD_H - 30, buf, kText);
    } else {
        draw_text(&font_small, 24, 560, "Ethos-U85 NPU is waiting", kMuted);
        draw_text(&font_small, 24, 600, "for your digit...", kMuted);
    }

    present();
}

/* ---------- inference ---------- */

static void infer(ui_state &s)
{
    static int8_t input_q[784];
    int8_t out_q[10];

    if (!preprocess(s.img28)) {
        s.has_pred = false;
        return;
    }
    for (int i = 0; i < 784; i++) {
        /* 0..255 ink -> MNIST normalization -> int8 model input */
        const float x = ((s.img28[i / 28][i % 28] / 255.0f) - MODEL_NORM_MEAN) / MODEL_NORM_STD;
        int32_t q = (int32_t)lroundf(x / MODEL_IN_SCALE) + MODEL_IN_ZP;
        input_q[i] = (int8_t)(q < -128 ? -128 : (q > 127 ? 127 : q));
    }
    if (executorch_run_inference(input_q, sizeof(input_q), out_q, sizeof(out_q)) != 0) {
        printf("ERROR: inference failed\r\n");
        s.has_pred = false;
        return;
    }
    s.npu_us = executorch_last_execute_cycles() / (SystemCoreClock / 1000000U);

    float maxl = -1e9f, sum = 0.0f;
    s.pred = 0;
    for (int d = 0; d < 10; d++) {
        s.prob[d] = (out_q[d] - MODEL_OUT_ZP) * MODEL_OUT_SCALE;
        if (s.prob[d] > maxl) { maxl = s.prob[d]; s.pred = d; }
    }
    for (int d = 0; d < 10; d++) { s.prob[d] = expf(s.prob[d] - maxl); sum += s.prob[d]; }
    for (int d = 0; d < 10; d++) s.prob[d] /= sum;
    s.has_pred = true;
}

/* ---------- platform ---------- */

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
    if (SysTick_Config(SystemCoreClock / 1000U) != 0) {
        halt("SysTick");
    }
    DCB->DEMCR |= DCB_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    se_services_port_init();
    if (SERVICES_clocks_enable_clock(se_services_s_handle, CLKEN_CLK_100M, true, &err) ||
        SERVICES_clocks_enable_clock(se_services_s_handle, CLKEN_HFOSC, true, &err)) {
        halt("MIPI clocks");
    }
    if (SERVICES_get_run_cfg(se_services_s_handle, &runp, &err)) {
        halt("get_run_cfg");
    }
    runp.memory_blocks  |= MRAM_MASK | SRAM0_MASK | SRAM1_MASK;
    runp.phy_pwr_gating |= MIPI_PLL_DPHY_MASK | MIPI_TX_DPHY_MASK | MIPI_RX_DPHY_MASK | LDO_PHY_MASK;
    if (SERVICES_set_run_cfg(se_services_s_handle, &runp, &err)) {
        halt("set_run_cfg");
    }

    fill_rect(0, 0, LCD_W, LCD_H, kBg);
    draw_text(&font_small, 20, 380, "Starting NPU + touch...", kText);
    present();

    ARM_DRIVER_CDC200 *cdc = &Driver_CDC200;
    if (cdc->Initialize(display_callback) != ARM_DRIVER_OK ||
        cdc->PowerControl(ARM_POWER_FULL) != ARM_DRIVER_OK ||
        cdc->Control(CDC200_CONFIGURE_DISPLAY, (uint32_t)fb) != ARM_DRIVER_OK ||
        cdc->Start() != ARM_DRIVER_OK) {
        halt("display start");
    }

    if (GT911.Initialize() != ARM_DRIVER_OK || GT911.PowerControl(ARM_POWER_FULL) != ARM_DRIVER_OK) {
        halt("GT911 touch init");
    }
    printf("[DRAW] touch ready\r\n");
}

static void map_touch(int rx, int ry, int *x, int *y)
{
    int a = rx, b = ry;
#if TOUCH_SWAP_XY
    int t = a; a = b; b = t;
#endif
#if TOUCH_FLIP_X
    a = LCD_W - 1 - a;
#endif
#if TOUCH_FLIP_Y
    b = LCD_H - 1 - b;
#endif
    *x = a;
    *y = b;
}

int main(void)
{
    platform_init();

    if (executorch_init(mnist_int8io_pte, mnist_int8io_pte_len) != 0) {
        halt("executorch_init");
    }
    executorch_set_verbose(0);

    static ui_state s;
    pad_clear();
    render(s);

    bool pen_down = false, dirty = false;
    int last_cx = 0, last_cy = 0;
    uint32_t last_touch = 0, last_frame = 0;

    for (;;) {
        ARM_TOUCH_STATE ts;
        const uint32_t t = now_ms();

        if (GT911.GetState(&ts) == ARM_DRIVER_OK && ts.numtouches > 0) {
            int x, y;
            map_touch(ts.coordinates[0].x, ts.coordinates[0].y, &x, &y);
            const bool new_stroke = !pen_down;
            if (new_stroke) {
                printf("[TOUCH] raw %d,%d -> screen %d,%d\r\n",
                       ts.coordinates[0].x, ts.coordinates[0].y, x, y);
            }

            if (x >= BTN_X && x < BTN_X + BTN_W && y >= BTN_Y && y < BTN_Y + BTN_H) {
                pad_clear();
                s.has_pred = false;
                pen_down = false;
            } else if (x >= PAD_X && x < PAD_X + PAD_PX && y >= PAD_Y && y < PAD_Y + PAD_PX) {
                /* starting a new digit after a pause clears the old one */
                if (new_stroke && !pad_empty && t - last_touch > 1500) {
                    pad_clear();
                }
                const int cx = (x - PAD_X) / PAD_SCALE, cy = (y - PAD_Y) / PAD_SCALE;
                if (pen_down) {
                    pad_line(last_cx, last_cy, cx, cy);
                } else {
                    pad_stamp(cx, cy);
                }
                last_cx = cx;
                last_cy = cy;
                pen_down = true;
            } else {
                pen_down = false;
            }
            s.cursor = true;
            s.cursor_x = x;
            s.cursor_y = y;
            last_touch = t;
            dirty = true;
        } else if (pen_down && t - last_touch > 150) {
            pen_down = false;   /* GT911 stops reporting ~10 ms after lift-off */
            s.cursor = false;
            dirty = true;
        } else if (s.cursor && t - last_touch > 150) {
            s.cursor = false;
            dirty = true;
        }

        /* re-run the NPU and redraw at most every 40 ms while things change */
        if (dirty && t - last_frame >= 40) {
            const uint32_t c0 = DWT->CYCCNT;
            infer(s);
            render(s);
            s.frame_ms = (DWT->CYCCNT - c0) / (SystemCoreClock / 1000U);
            if (s.has_pred && !pen_down) {
                printf("[DRAW] pred=%d p=%d%% npu=%lu us frame=%lu ms\r\n", s.pred,
                       (int)(s.prob[s.pred] * 100.0f), (unsigned long)s.npu_us,
                       (unsigned long)s.frame_ms);
            }
            dirty = false;
            last_frame = t;
        }
    }
}
