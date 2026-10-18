/* DK-E8 hardware self-test dashboard.
 *
 * Brings up each peripheral independently and shows its live state on the LCD:
 *   RGB LEDs (cycling), joystick (5 switches), potentiometer (ADC121),
 *   IMU (BMI323 or ICM-42670P over I3C), PDM microphones (level meter),
 *   GT911 touch, and the Ethos-U85 NPU (MNIST test digit).
 * Rows turn green when verified: automatically where possible, otherwise once
 * you exercise them (press each joystick direction, turn the pot, touch, speak).
 *
 * Only one IMU driver can own the shared I3C bus: build with HW_TEST_IMU_ICM=1
 * to test the ICM-42670P instead of the BMI323.
 */
#include "RTE_Components.h"
#include CMSIS_device_header

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "Driver_ADC.h"
#include "Driver_CDC200.h"
#include "Driver_IMU.h"
#include "Driver_IO.h"
#include "Driver_PDM.h"
#include "Driver_Touch_Screen.h"
#include "board_config.h"
#include "executorch_runner.h"
#include <stdbool.h>
#include "se_services_port.h"

#include "assets/fonts.h"
#include "assets/mnist_model_data.h"
#include "assets/model_io.h"
#include "assets/test_digits.h"

#ifndef HW_TEST_IMU_ICM
#define HW_TEST_IMU_ICM 0
#endif

#define LCD_W (RTE_PANEL_HACTIVE_TIME)
#define LCD_H (RTE_PANEL_VACTIVE_LINE)

static uint8_t fb[LCD_H][LCD_W][3] __attribute__((section(".bss.lcd_frame_buf"), aligned(16)));
static uint8_t back[LCD_H][LCD_W][3] __attribute__((section(".bss.at_sram1"), aligned(16)));

extern ARM_DRIVER_CDC200 Driver_CDC200;
extern ARM_DRIVER_TOUCH_SCREEN GT911;
extern ARM_DRIVER_PDM Driver_PDM;
extern ARM_DRIVER_ADC ARM_Driver_ADC12(BOARD_POTENTIOMETER_ADC12_INSTANCE);
#if HW_TEST_IMU_ICM
extern ARM_DRIVER_IMU ICM42670P;
#else
extern ARM_DRIVER_IMU BMI323;
#endif
extern ARM_DRIVER_GPIO ARM_Driver_GPIO_(BOARD_LEDRGB0_R_GPIO_PORT);
extern ARM_DRIVER_GPIO ARM_Driver_GPIO_(BOARD_LEDRGB0_G_GPIO_PORT);
extern ARM_DRIVER_GPIO ARM_Driver_GPIO_(BOARD_LEDRGB0_B_GPIO_PORT);
extern ARM_DRIVER_GPIO ARM_Driver_GPIO_(BOARD_LEDRGB1_R_GPIO_PORT);
extern ARM_DRIVER_GPIO ARM_Driver_GPIO_(BOARD_JOY_SW_A_GPIO_PORT);

#if HW_TEST_IMU_ICM
static ARM_DRIVER_IMU *imu_drv = &ICM42670P;
static const char *kImuName = "ICM-42670P";
#else
static ARM_DRIVER_IMU *imu_drv = &BMI323;
static const char *kImuName = "BMI323";
#endif

typedef struct { uint8_t r, g, b; } rgb;
static const rgb kBg     = {16, 20, 32};
static const rgb kHeader = {32, 40, 64};
static const rgb kPanel  = {26, 32, 48};
static const rgb kText   = {235, 238, 245};
static const rgb kMuted  = {140, 150, 170};
static const rgb kGood   = {60, 200, 110};
static const rgb kBad    = {235, 80, 70};
static const rgb kWait   = {240, 190, 60};
static const rgb kBar    = {70, 120, 200};

/* ---------- time + drawing ---------- */

static volatile uint32_t g_ms;
void SysTick_Handler(void) { g_ms++; }

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

static void draw_text(int x, int y, const char *s, rgb c)
{
    const font_t *f = &font_small;
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
    SCB_CleanDCache();
}

/* ---------- per-peripheral state ---------- */

typedef enum { ST_PENDING, ST_OK, ST_FAIL, ST_WAIT } status_t;
typedef struct {
    status_t st;
    char msg[40];
} check_t;
static void set_st(check_t *c, status_t st, const char *msg)
{
    c->st = st;
    snprintf(c->msg, sizeof(c->msg), "%s", msg);
}

static check_t c_leds, c_joy, c_pot, c_imu, c_mic, c_touch, c_npu;

/* LEDs */
static ARM_DRIVER_GPIO *const led_drv[2][3] = {
    {&ARM_Driver_GPIO_(BOARD_LEDRGB0_R_GPIO_PORT), &ARM_Driver_GPIO_(BOARD_LEDRGB0_G_GPIO_PORT),
     &ARM_Driver_GPIO_(BOARD_LEDRGB0_B_GPIO_PORT)},
    {&ARM_Driver_GPIO_(BOARD_LEDRGB1_R_GPIO_PORT), &ARM_Driver_GPIO_(BOARD_LEDRGB1_G_GPIO_PORT),
     &ARM_Driver_GPIO_(BOARD_LEDRGB1_B_GPIO_PORT)},
};
static const uint8_t led_pin[2][3] = {
    {BOARD_LEDRGB0_R_GPIO_PIN, BOARD_LEDRGB0_G_GPIO_PIN, BOARD_LEDRGB0_B_GPIO_PIN},
    {BOARD_LEDRGB1_R_GPIO_PIN, BOARD_LEDRGB1_G_GPIO_PIN, BOARD_LEDRGB1_B_GPIO_PIN},
};
static int led_phase;   /* 0 R, 1 G, 2 B, 3 white, 4 off */

static void leds_set(int phase)
{
    static const uint8_t on[5][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 1}, {0, 0, 0}};
    for (int l = 0; l < 2; l++)
        for (int c = 0; c < 3; c++)
            led_drv[l][c]->SetValue(led_pin[l][c], on[phase][c] ? GPIO_PIN_OUTPUT_STATE_HIGH
                                                                : GPIO_PIN_OUTPUT_STATE_LOW);
}

static void leds_init(void)
{
    for (int l = 0; l < 2; l++) {
        for (int c = 0; c < 3; c++) {
            ARM_DRIVER_GPIO *d = led_drv[l][c];
            if (d->Initialize(led_pin[l][c], NULL) || d->PowerControl(led_pin[l][c], ARM_POWER_FULL) ||
                d->SetDirection(led_pin[l][c], GPIO_PIN_DIRECTION_OUTPUT)) {
                set_st(&c_leds, ST_FAIL, "GPIO init failed");
                return;
            }
        }
    }
    leds_set(0);
    set_st(&c_leds, ST_WAIT, "check both LEDs: R,G,B,W");
}

/* Joystick (LPGPIO port 15, pins 0..4) */
static ARM_DRIVER_GPIO *joy_drv = &ARM_Driver_GPIO_(BOARD_JOY_SW_A_GPIO_PORT);
static const uint8_t joy_pin[5] = {BOARD_JOY_SW_A_GPIO_PIN, BOARD_JOY_SW_B_GPIO_PIN, BOARD_JOY_SW_C_GPIO_PIN,
                                   BOARD_JOY_SW_D_GPIO_PIN, BOARD_JOY_SW_CENTER_GPIO_PIN};
static const char *joy_name[5] = {"A", "B", "C", "D", "CTR"};
static uint32_t joy_idle[5];
static bool joy_now[5], joy_seen[5];

static void joy_init(void)
{
    for (int i = 0; i < 5; i++) {
        if (joy_drv->Initialize(joy_pin[i], NULL) || joy_drv->PowerControl(joy_pin[i], ARM_POWER_FULL) ||
            joy_drv->SetDirection(joy_pin[i], GPIO_PIN_DIRECTION_INPUT)) {
            set_st(&c_joy, ST_FAIL, "GPIO init failed");
            return;
        }
        joy_drv->GetValue(joy_pin[i], &joy_idle[i]);   /* level with nothing pressed */
    }
    set_st(&c_joy, ST_WAIT, "press all 5 directions");
}

static void joy_poll(void)
{
    if (c_joy.st == ST_FAIL) return;
    int seen = 0;
    for (int i = 0; i < 5; i++) {
        uint32_t v = joy_idle[i];
        joy_drv->GetValue(joy_pin[i], &v);
        joy_now[i] = (v != joy_idle[i]);
        if (joy_now[i] && !joy_seen[i]) {
            joy_seen[i] = true;
            printf("[HW] joystick %s pressed (idle level %lu)\r\n", joy_name[i], (unsigned long)joy_idle[i]);
        }
        seen += joy_seen[i];
    }
    if (seen == 5) set_st(&c_joy, ST_OK, "all 5 switches work");
}

/* Potentiometer: blue R79 knob next to the joystick, 0..1.8 V into ADC121 input 1 */
static ARM_DRIVER_ADC *adc_drv = &ARM_Driver_ADC12(BOARD_POTENTIOMETER_ADC12_INSTANCE);
static volatile bool adc_done;
static volatile uint32_t adc_value;
static uint32_t pot_value, pot_min = 0xFFFFFFFF, pot_max;
static void adc_cb(uint32_t event, uint8_t channel, uint32_t value)
{
    (void)channel;
    if (event & ARM_ADC_EVENT_CONVERSION_COMPLETE) {
        adc_value = value;
        adc_done  = true;
    }
}

static void pot_init(void)
{
    if (adc_drv->Initialize(adc_cb) || adc_drv->PowerControl(ARM_POWER_FULL) ||
        adc_drv->Control(ARM_ADC_CONVERSION_MODE_CTRL, ARM_ADC_SINGLE_SHOT_CH_CONV) ||
        adc_drv->Control(ARM_ADC_CHANNEL_INIT_VAL, BOARD_POTENTIOMETER_ADC12_INPUT)) {
        set_st(&c_pot, ST_FAIL, "ADC init failed");
        return;
    }
    set_st(&c_pot, ST_WAIT, "turn blue knob R79 end to end");
}

static void pot_poll(void)
{
    if (c_pot.st == ST_FAIL) return;
    adc_done = false;
    if (adc_drv->Start() != ARM_DRIVER_OK) {
        set_st(&c_pot, ST_FAIL, "ADC start failed");
        return;
    }
    const uint32_t t0 = g_ms;
    while (!adc_done && g_ms - t0 < 50) {
    }
    adc_drv->Stop();
    if (!adc_done) {
        set_st(&c_pot, ST_FAIL, "ADC conversion timeout");
        return;
    }
    pot_value = adc_value;
    if (pot_value < pot_min) pot_min = pot_value;
    if (pot_value > pot_max) pot_max = pot_value;
    if (c_pot.st == ST_WAIT && pot_max - pot_min > 2000) set_st(&c_pot, ST_OK, "full range seen");
}

/* IMU */
static float imu_acc[3], imu_gyro[3], imu_temp;
static float sv(const ARM_IMU_SENSOR_VALUE *v) { return v->int_val + v->fract_val / 1e6f; }

static void imu_init(void)
{
    if (imu_drv->Initialize() != ARM_DRIVER_OK || imu_drv->PowerControl(ARM_POWER_FULL) != ARM_DRIVER_OK) {
        set_st(&c_imu, ST_FAIL, "init failed (I3C)");
        return;
    }
    set_st(&c_imu, ST_PENDING, "reading...");
}

static void imu_poll(void)
{
    if (c_imu.st == ST_FAIL) return;
    ARM_IMU_COORDINATES a, g;
    ARM_IMU_SENSOR_VALUE t;
    if (imu_drv->Control(IMU_GET_ACCELEROMETER_DATA, (uint32_t)&a) != ARM_DRIVER_OK ||
        imu_drv->Control(IMU_GET_GYROSCOPE_DATA, (uint32_t)&g) != ARM_DRIVER_OK) {
        set_st(&c_imu, ST_FAIL, "read failed");
        return;
    }
    imu_acc[0] = sv(&a.x); imu_acc[1] = sv(&a.y); imu_acc[2] = sv(&a.z);
    imu_gyro[0] = sv(&g.x); imu_gyro[1] = sv(&g.y); imu_gyro[2] = sv(&g.z);
    if (imu_drv->Control(IMU_GET_TEMPERATURE_DATA, (uint32_t)&t) == ARM_DRIVER_OK) imu_temp = sv(&t);
    const float mag = sqrtf(imu_acc[0] * imu_acc[0] + imu_acc[1] * imu_acc[1] + imu_acc[2] * imu_acc[2]);
    if (mag > 0.7f && mag < 1.3f) {
        set_st(&c_imu, ST_OK, "gravity ~1 g: plausible");
    } else {
        char m[40];
        snprintf(m, sizeof(m), "|a| = %d mg: implausible", (int)(mag * 1000));
        set_st(&c_imu, ST_WAIT, m);
    }
}

/* PDM microphones: channels 4+5 on PDM_D2, 8 kHz, interleaved 16-bit.
 * The gain register is linear (measured: 4x the value = +12 dB). Alif's demo values
 * (0x0D/0x13) leave speech near -50 dBFS; 0x800 puts room noise near -54 dBFS and
 * speech around -30. Channel 5 keeps the demo's 1.46x ratio to match the mics. */
#define MIC_GAIN_CH4 0x800
#define MIC_GAIN_CH5 0xBB0
#define MIC_SAMPLES 1600   /* 100 ms of both channels */
static int16_t mic_buf[MIC_SAMPLES];
static volatile int mic_event;   /* 1 complete, 2 error */
static float mic_db = -120.0f, mic_peak_db = -120.0f;
static uint32_t mic_blocks;
static const uint32_t ch4_fir[18] = {0x00000001, 0x00000003, 0x00000003, 0x000007F4, 0x00000004, 0x000007ED,
                                     0x000007F5, 0x000007F4, 0x000007D3, 0x000007FE, 0x000007BC, 0x000007E5,
                                     0x000007D9, 0x00000793, 0x00000029, 0x0000072C, 0x00000072, 0x000002FD};
static const uint32_t ch5_fir[18] = {0x00000000, 0x000007FF, 0x00000000, 0x00000004, 0x00000004, 0x000007FC,
                                     0x00000000, 0x000007FB, 0x000007E4, 0x00000000, 0x0000002B, 0x00000009,
                                     0x00000016, 0x00000049, 0x00000793, 0x000006F8, 0x00000045, 0x00000178};

static void pdm_cb(uint32_t event)
{
    if (event & ARM_PDM_EVENT_ERROR) mic_event = 2;
    else if (event & ARM_PDM_EVENT_CAPTURE_COMPLETE) mic_event = 1;
}

static void mic_init(void)
{
    uint32_t err;
    ARM_DRIVER_PDM *p = &Driver_PDM;
    PDM_CH_CONFIG cfg;
    if (SERVICES_clocks_enable_clock(se_services_s_handle, CLKEN_HFOSCx2, true, &err) ||
        p->Initialize(pdm_cb) || p->PowerControl(ARM_POWER_FULL) ||
        p->Control(ARM_PDM_SELECT_CHANNEL, ARM_PDM_MASK_CHANNEL_4 | ARM_PDM_MASK_CHANNEL_5, 0) ||
        p->Control(ARM_PDM_MODE, ARM_PDM_MODE_AUDIOFREQ_8K_DECM_64, 0) ||
        p->Control(ARM_PDM_BYPASS_IIR_FILTER, 1, 0) ||
        p->Control(ARM_PDM_CHANNEL_PHASE, 4, 0x1F) || p->Control(ARM_PDM_CHANNEL_GAIN, 4, MIC_GAIN_CH4) ||
        p->Control(ARM_PDM_CHANNEL_PEAK_DETECT_TH, 4, 0x00060002) ||
        p->Control(ARM_PDM_CHANNEL_PEAK_DETECT_ITV, 4, 0x0004002D) ||
        p->Control(ARM_PDM_CHANNEL_PHASE, 5, 0x03) || p->Control(ARM_PDM_CHANNEL_GAIN, 5, MIC_GAIN_CH5) ||
        p->Control(ARM_PDM_CHANNEL_PEAK_DETECT_TH, 5, 0x00060002) ||
        p->Control(ARM_PDM_CHANNEL_PEAK_DETECT_ITV, 5, 0x00020027)) {
        set_st(&c_mic, ST_FAIL, "PDM init failed");
        return;
    }
    cfg.ch_num = 4;
    memcpy(cfg.ch_fir_coef, ch4_fir, sizeof(cfg.ch_fir_coef));
    cfg.ch_iir_coef = 0x00000004;
    if (p->Config(&cfg)) { set_st(&c_mic, ST_FAIL, "PDM ch4 config"); return; }
    cfg.ch_num = 5;
    memcpy(cfg.ch_fir_coef, ch5_fir, sizeof(cfg.ch_fir_coef));
    cfg.ch_iir_coef = 0x00000004;
    if (p->Config(&cfg)) { set_st(&c_mic, ST_FAIL, "PDM ch5 config"); return; }

    mic_event = 0;
    if (p->Receive(mic_buf, MIC_SAMPLES) != ARM_DRIVER_OK) {
        set_st(&c_mic, ST_FAIL, "PDM receive failed");
        return;
    }
    set_st(&c_mic, ST_WAIT, "make some noise");
}

static void mic_poll(void)
{
    if (c_mic.st == ST_FAIL || mic_event == 0) return;
    if (mic_event == 2) {
        set_st(&c_mic, ST_FAIL, "PDM error event");
        return;
    }
    /* RMS over the block, DC removed */
    float mean = 0.0f, acc = 0.0f;
    for (int i = 0; i < MIC_SAMPLES; i++) mean += mic_buf[i];
    mean /= MIC_SAMPLES;
    for (int i = 0; i < MIC_SAMPLES; i++) { float d = mic_buf[i] - mean; acc += d * d; }
    const float rms = sqrtf(acc / MIC_SAMPLES);
    mic_db = rms > 0.5f ? 20.0f * log10f(rms / 32768.0f) : -120.0f;
    mic_blocks++;
    if (mic_blocks > 3 && mic_db > mic_peak_db) mic_peak_db = mic_db;   /* skip start-up blocks */
    if (mic_blocks > 3 && mic_db > -90.0f && c_mic.st != ST_OK && mic_peak_db - mic_db > 15.0f) {
        set_st(&c_mic, ST_OK, "level responds to sound");
    }
    mic_event = 0;
    Driver_PDM.Receive(mic_buf, MIC_SAMPLES);
}

/* Touch */
static int touch_x = -1, touch_y = -1;
static uint32_t touch_count;
static void touch_init(void)
{
    if (GT911.Initialize() != ARM_DRIVER_OK || GT911.PowerControl(ARM_POWER_FULL) != ARM_DRIVER_OK) {
        set_st(&c_touch, ST_FAIL, "GT911 init failed");
        return;
    }
    set_st(&c_touch, ST_WAIT, "touch the screen");
}
static void touch_poll(void)
{
    ARM_TOUCH_STATE ts;
    if (c_touch.st == ST_FAIL) return;
    if (GT911.GetState(&ts) == ARM_DRIVER_OK && ts.numtouches > 0) {
        touch_x = ts.coordinates[0].x;
        touch_y = ts.coordinates[0].y;
        if (++touch_count > 3) set_st(&c_touch, ST_OK, "touch events received");
    }
}

/* NPU */
static uint32_t npu_us;
static int npu_runs, npu_correct;
static bool npu_alive;  /* runtime initialized and no inference has errored */
static void npu_init(void)
{
    if (executorch_init(mnist_int8io_pte, mnist_int8io_pte_len) != 0) {
        set_st(&c_npu, ST_FAIL, "ExecuTorch init failed");
        return;
    }
    executorch_set_verbose(0);
    npu_alive = true;
    set_st(&c_npu, ST_PENDING, "running...");
}
static void npu_poll(void)
{
    if (!npu_alive) return;
    static int8_t input_q[784];
    int8_t out[10];
    const int idx = npu_runs % NUM_TEST_DIGITS;
    for (int i = 0; i < 784; i++) {
        const float x = ((test_digits[idx][i] / 127.0f) - MODEL_NORM_MEAN) / MODEL_NORM_STD;
        int32_t q = (int32_t)lroundf(x / MODEL_IN_SCALE) + MODEL_IN_ZP;
        input_q[i] = (int8_t)(q < -128 ? -128 : (q > 127 ? 127 : q));
    }
    if (executorch_run_inference(input_q, sizeof(input_q), out, sizeof(out)) != 0) {
        npu_alive = false;
        set_st(&c_npu, ST_FAIL, "inference failed");
        return;
    }
    npu_us = executorch_last_execute_cycles() / (SystemCoreClock / 1000000U);
    int pred = 0;
    for (int d = 1; d < 10; d++) if (out[d] > out[pred]) pred = d;
    npu_runs++;
    npu_correct += (pred == test_digit_label[idx]);
    char m[40];
    snprintf(m, sizeof(m), "MNIST %d/%d correct", npu_correct, npu_runs);
    /* The model misreads one hard test digit (#18, a messy 3) and it comes 4th, so
     * partial counts look worse than the real 9/10: judge only after a full pass. */
    if (npu_runs < NUM_TEST_DIGITS)
        set_st(&c_npu, ST_PENDING, m);
    else
        set_st(&c_npu, npu_correct * 100 >= npu_runs * 80 ? ST_OK : ST_FAIL, m);
}

/* ---------- dashboard ---------- */

static rgb st_color(status_t s)
{
    return s == ST_OK ? kGood : s == ST_FAIL ? kBad : s == ST_WAIT ? kWait : kMuted;
}

static int row(int y, int h, const char *title, const check_t *c)
{
    fill_rect(8, y, LCD_W - 16, h, kPanel);
    fill_rect(8, y, 6, h, st_color(c->st));
    draw_text(22, y + 4, title, kText);
    const char *tag = c->st == ST_OK ? "OK" : c->st == ST_FAIL ? "FAIL" : c->st == ST_WAIT ? "..." : "-";
    draw_text(LCD_W - 20 - (int)strlen(tag) * font_small.w, y + 4, tag, st_color(c->st));
    draw_text(22, y + 34, c->msg, kMuted);
    return y + h + 6;
}

static void hbar(int x, int y, int w, int h, float frac, rgb c)
{
    fill_rect(x, y, w, h, kBg);
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    fill_rect(x, y, (int)(w * frac), h, c);
}

static void render(void)
{
    char buf[48];
    fill_rect(0, 0, LCD_W, LCD_H, kBg);
    fill_rect(0, 0, LCD_W, 52, kHeader);
    draw_text(16, 12, "DK-E8 hardware test", kText);
    snprintf(buf, sizeof(buf), "%lus", (unsigned long)(g_ms / 1000));
    draw_text(LCD_W - 16 - (int)strlen(buf) * font_small.w, 12, buf, kMuted);

    int y = 60;

    /* LEDs */
    static const rgb sw[5] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}, {0, 0, 0}};
    int y0 = y;
    y = row(y, 70, "RGB LEDs", &c_leds);
    fill_rect(LCD_W - 100, y0 + 38, 36, 24, sw[led_phase]);
    fill_rect(LCD_W - 56, y0 + 38, 36, 24, sw[led_phase]);

    /* Joystick */
    y0 = y;
    y = row(y, 104, "Joystick", &c_joy);
    for (int i = 0; i < 5; i++) {
        const int bx = 22 + i * 88, by = y0 + 66;
        fill_rect(bx, by, 80, 30, joy_now[i] ? kWait : (joy_seen[i] ? kGood : kBg));
        draw_text(bx + (80 - (int)strlen(joy_name[i]) * font_small.w) / 2, by + 1, joy_name[i], kText);
    }

    /* Potentiometer */
    y0 = y;
    y = row(y, 104, "Potentiometer (ADC)", &c_pot);
    hbar(22, y0 + 66, 300, 28, pot_value / 4095.0f, kBar);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)pot_value);
    draw_text(336, y0 + 66, buf, kText);

    /* IMU */
    y0 = y;
    snprintf(buf, sizeof(buf), "IMU %s (I3C)", kImuName);
    y = row(y, 196, buf, &c_imu);
    static const char *ax[3] = {"x", "y", "z"};
    for (int i = 0; i < 3; i++) {
        const int by = y0 + 66 + i * 32;
        draw_text(22, by, ax[i], kMuted);
        fill_rect(44 + 150, by + 2, 2, 24, kMuted);
        float f = imu_acc[i] / 2.0f;   /* +-2 g full scale */
        if (f > 1) f = 1;
        if (f < -1) f = -1;
        if (f >= 0) fill_rect(44 + 150, by + 4, (int)(150 * f), 20, kBar);
        else fill_rect(44 + 150 + (int)(150 * f), by + 4, (int)(-150 * f), 20, kBar);
        snprintf(buf, sizeof(buf), "%5d mg", (int)(imu_acc[i] * 1000));
        draw_text(352, by, buf, kText);
    }
    snprintf(buf, sizeof(buf), "gyro %d %d %d dps  %dC", (int)imu_gyro[0], (int)imu_gyro[1],
             (int)imu_gyro[2], (int)imu_temp);
    draw_text(22, y0 + 162, buf, kMuted);

    /* Microphone */
    y0 = y;
    y = row(y, 96, "Microphones (PDM)", &c_mic);
    hbar(22, y0 + 66, 300, 28, (mic_db + 90.0f) / 90.0f, mic_db > -20 ? kBad : kGood);
    snprintf(buf, sizeof(buf), "%d dBFS", (int)mic_db);
    draw_text(336, y0 + 66, buf, kText);

    /* Touch + NPU */
    y0 = y;
    y = row(y, 66, "Touch (GT911)", &c_touch);
    if (touch_x >= 0) {
        snprintf(buf, sizeof(buf), "%d,%d", touch_x, touch_y);
        draw_text(LCD_W - 80 - (int)strlen(buf) * font_small.w, y0 + 4, buf, kMuted);
    }
    y0 = y;
    y = row(y, 66, "NPU Ethos-U85", &c_npu);
    snprintf(buf, sizeof(buf), "%lu us", (unsigned long)npu_us);
    draw_text(LCD_W - 80 - (int)strlen(buf) * font_small.w, y0 + 4, buf, kMuted);

    present();
}

/* ---------- platform ---------- */

static void halt(const char *why)
{
    printf("ERROR: %s\r\n", why);
    while (1) __WFI();
}

static void display_callback(uint32_t event)
{
    if (event & ARM_CDC_DSI_ERROR_EVENT) halt("DSI error");
}

static void platform_init(void)
{
    uint32_t err;
    run_profile_t runp = {0};

    if (board_pins_config() != 0 || board_gpios_config() != 0) halt("board pin/gpio config");
    if (SysTick_Config(SystemCoreClock / 1000U) != 0) halt("SysTick");
    DCB->DEMCR |= DCB_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    se_services_port_init();
    if (SERVICES_clocks_enable_clock(se_services_s_handle, CLKEN_CLK_100M, true, &err) ||
        SERVICES_clocks_enable_clock(se_services_s_handle, CLKEN_HFOSC, true, &err)) {
        halt("MIPI clocks");
    }
    if (SERVICES_get_run_cfg(se_services_s_handle, &runp, &err)) halt("get_run_cfg");
    runp.memory_blocks  |= MRAM_MASK | SRAM0_MASK | SRAM1_MASK;
    runp.phy_pwr_gating |= MIPI_PLL_DPHY_MASK | MIPI_TX_DPHY_MASK | MIPI_RX_DPHY_MASK | LDO_PHY_MASK;
    runp.vdd_ioflex_3V3  = IOFLEX_LEVEL_1V8;   /* I3C (IMUs) on flex I/O P7_6/P7_7 needs 1.8 V */
    if (SERVICES_set_run_cfg(se_services_s_handle, &runp, &err)) halt("set_run_cfg");

    fill_rect(0, 0, LCD_W, LCD_H, kBg);
    draw_text(20, 380, "Starting hardware test...", kText);
    present();

    ARM_DRIVER_CDC200 *cdc = &Driver_CDC200;
    if (cdc->Initialize(display_callback) != ARM_DRIVER_OK || cdc->PowerControl(ARM_POWER_FULL) != ARM_DRIVER_OK ||
        cdc->Control(CDC200_CONFIGURE_DISPLAY, (uint32_t)fb) != ARM_DRIVER_OK || cdc->Start() != ARM_DRIVER_OK) {
        halt("display start");
    }
}

int main(void)
{
    platform_init();

    /* each subsystem reports its own status; a failure doesn't stop the others */
    printf("[HW] leds\r\n");     leds_init();
    printf("[HW] joystick\r\n"); joy_init();
    printf("[HW] pot\r\n");      pot_init();
    printf("[HW] touch\r\n");    touch_init();
    printf("[HW] npu\r\n");      npu_init();
    printf("[HW] mic\r\n");      mic_init();
    printf("[HW] imu\r\n");      imu_init();
    printf("[HW] init done\r\n");

    uint32_t last_led = 0, last_slow = 0, last_npu = 0, last_log = 0;
    for (;;) {
        const uint32_t t = g_ms;
        joy_poll();
        touch_poll();
        mic_poll();
        if (c_leds.st != ST_FAIL && t - last_led >= 1000) {
            led_phase = (led_phase + 1) % 5;
            leds_set(led_phase);
            last_led = t;
        }
        if (t - last_npu >= 1000) {
            npu_poll();
            last_npu = t;
        }
        if (t - last_slow >= 100) {
            pot_poll();
            imu_poll();
            render();
            last_slow = t;
        }
        if (t - last_log >= 2000) {
            printf("[HW] t=%lus led=%d joy=%d pot=%lu(%lu..%lu) imu=%d,%d,%d mg mic=%d dB(peak %d) "
                   "touch=%lu npu=%lu us %d/%d | st L%d J%d P%d I%d M%d T%d N%d\r\n",
                   (unsigned long)(t / 1000), c_leds.st, c_joy.st, (unsigned long)pot_value,
                   (unsigned long)(pot_min == 0xFFFFFFFF ? 0 : pot_min), (unsigned long)pot_max,
                   (int)(imu_acc[0] * 1000), (int)(imu_acc[1] * 1000), (int)(imu_acc[2] * 1000),
                   (int)mic_db, (int)mic_peak_db, (unsigned long)touch_count, (unsigned long)npu_us,
                   npu_correct, npu_runs, c_leds.st, c_joy.st, c_pot.st, c_imu.st, c_mic.st,
                   c_touch.st, c_npu.st);
            last_log = t;
        }
    }
}
