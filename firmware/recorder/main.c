/* Sound Sense recorder: captures both PDM mics (16 kHz, interleaved int16: mic 4, mic 5) into the
 * 64 MB OSPI0 PSRAM, about 16 minutes. Used to compare our mics with the training data, to measure
 * the mic spacing (docs/HANDOFF-gates.md C4) and to capture teaching sounds for G3.
 *
 * Control: joystick centre starts and stops a take. Takes are appended one after another; the host
 * reads rec_take_off[i] / rec_take_len[i] over J-Link. The host can also write rec_cmd (1 = start a
 * new take, 2 = stop, 3 = forget all takes). tools/pull_audio.sh reads a take and tools/raw2wav.py
 * turns it into a stereo wav.
 *
 * Audio comes from ../app/kws.c through its kws_block_hook; the PSRAM driver and init from
 * ../psram_test (pack 2.2.1 driver, vendored), so this project must not link the pack's OSPI. */
#include "RTE_Components.h"
#include CMSIS_device_header

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "Driver_IO.h"
#include "kws.h"
#include "pinconf.h"
#include "ss_board.h"
#include "ss_gfx.h"
#include "APS512XXN_PSRAM.h"
#include "ospi.h"
#include "ospi_psram_xip.h"

#define RAM_BASE      0xA0000000u
#define RAM_SIZE      (64u << 20)
#define RESET_PORT    15
#define RAM_RESET_PIN 6

/* Host interface (read and written over J-Link; see tools/pull_audio.sh) */
volatile uint32_t rec_cmd;       /* 1 start, 2 stop; cleared when taken */
volatile uint32_t rec_state;     /* 0 idle, 1 recording */
volatile uint32_t rec_bytes;     /* bytes used in the PSRAM, from RAM_BASE (all takes) */
volatile uint32_t rec_takes;     /* takes recorded */
#define MAX_TAKES 64
volatile uint32_t rec_take_off[MAX_TAKES], rec_take_len[MAX_TAKES];
volatile uint32_t rec_dropped;   /* audio blocks the capture dropped (kws_audio_dropped) */

static float lvl_db[2] = {-120.0f, -120.0f}, peak_db[2] = {-120.0f, -120.0f};

/* ---------- PSRAM (same settings as psram_test) ---------- */

extern ARM_DRIVER_GPIO ARM_Driver_GPIO_(RESET_PORT);
static ARM_DRIVER_GPIO *const reset_gpio = &ARM_Driver_GPIO_(RESET_PORT);

static void delay_us(uint32_t us)
{
    const uint32_t t0 = ss_cycles(), n = us * (SystemCoreClock / 1000000u);
    while (ss_cycles() - t0 < n) {}
}

static int32_t ram_init_hook(OSPI_Type *ospi, AES_Type *aes)
{
    ospi_transfer_t t = {0};
    uint32_t reset_cmd = APS512XXN_CMD_GLOBAL_RESET;
    ospi_set_dfs(ospi, APS512XXN_OSPI_REG_DFS);
    t.spi_frf      = SPI_FRF_OCTAL;
    t.ddr          = 1;
    t.inst_len     = SPI_INST_L_8_BIT;
    t.addr_len     = SPI_ADDR_L_0_BIT;
    t.dummy_cycle  = APS512XXN_RESET_WAIT_CYCLES;
    t.tx_total_cnt = 1;
    t.tx_buff      = &reset_cmd;
    ospi_send_polling(ospi, &t);
    delay_us(10);
    return aps512xxn_psram_init(ospi, aes);
}

static ospi_psram_xip_config ram_cfg = {
    .instance       = OSPI_INSTANCE_0,
    .bus_speed      = 100000000,
    .ram_init       = ram_init_hook,
    .ddr_drive_edge = 1,
    .rxds_delay     = 11,
    .wait_cycles    = RTE_APS512XXN_PSRAM_WAIT_CYCLES,
    .slave_select   = 0,
    .dfs            = 32,
    .spi_frf        = OSPI_SPI_FRF_OCTAL,
    .ram_type       = RAM_TYPE_PSRAM,
    .config_mode    = CONFIG_MODE_USER_PARAMETERS,
    .signal_delay   = 22,
};

static int psram_init(void)
{
    if (pinconf_set(RESET_PORT, RAM_RESET_PIN, PINMUX_ALTERNATE_FUNCTION_0, 0) ||
        reset_gpio->Initialize(RAM_RESET_PIN, NULL) != ARM_DRIVER_OK ||
        reset_gpio->PowerControl(RAM_RESET_PIN, ARM_POWER_FULL) != ARM_DRIVER_OK ||
        reset_gpio->SetDirection(RAM_RESET_PIN, GPIO_PIN_DIRECTION_OUTPUT) != ARM_DRIVER_OK) {
        return -1;
    }
    reset_gpio->SetValue(RAM_RESET_PIN, GPIO_PIN_OUTPUT_STATE_LOW);
    delay_us(10);
    reset_gpio->SetValue(RAM_RESET_PIN, GPIO_PIN_OUTPUT_STATE_HIGH);
    delay_us(500);
    if (ospi_psram_xip_init(&ram_cfg) < 0) return -2;
    volatile uint32_t *p = (volatile uint32_t *)RAM_BASE;
    p[0] = 0x5EC0DE01u;
    SCB_CleanInvalidateDCache();
    return p[0] == 0x5EC0DE01u ? 0 : -3;
}

/* ---------- capture (called from kws_audio_poll for every 20 ms block) ---------- */

void kws_block_hook(const int16_t *stereo, int n, uint32_t done_cycles)
{
    (void)done_cycles;
    float acc[2] = {0, 0}, pk[2] = {0, 0};
    for (int i = 0; i < n; i++) {
        for (int c = 0; c < 2; c++) {
            const float s = stereo[2 * i + c] / 32768.0f;
            acc[c] += s * s;
            if (fabsf(s) > pk[c]) pk[c] = fabsf(s);
        }
    }
    for (int c = 0; c < 2; c++) {
        lvl_db[c] = acc[c] > 0 ? 10.0f * log10f(acc[c] / n) : -120.0f;
        const float p = pk[c] > 0 ? 20.0f * log10f(pk[c]) : -120.0f;
        if (p > peak_db[c]) peak_db[c] = p;
    }
    if (rec_state != 1) return;
    const uint32_t len = (uint32_t)n * 2u * sizeof(int16_t);
    if (rec_bytes + len > RAM_SIZE) {
        rec_state = 0;   /* full */
        return;
    }
    uint8_t *dst = (uint8_t *)(RAM_BASE + rec_bytes);
    memcpy(dst, stereo, len);
    SCB_CleanDCache_by_Addr(dst, (int32_t)len);   /* J-Link reads the RAM, not the cache */
    rec_bytes += len;
    rec_take_len[rec_takes - 1] += len;
}

static void start_take(void)
{
    if (rec_takes >= MAX_TAKES) return;
    rec_take_off[rec_takes] = rec_bytes;
    rec_take_len[rec_takes] = 0;
    rec_takes++;
    peak_db[0] = peak_db[1] = -120.0f;
    rec_state = 1;
    printf("[REC] take %lu started\r\n", (unsigned long)rec_takes);
}

static void stop_take(void)
{
    rec_state = 0;
    const uint32_t n = rec_take_len[rec_takes - 1];
    printf("[REC] take %lu stopped: %lu bytes (%lu.%01lu s), peaks %d / %d dBFS, dropped %lu\r\n",
           (unsigned long)rec_takes, (unsigned long)n, (unsigned long)(n / 64000u),
           (unsigned long)(n % 64000u / 6400u), (int)peak_db[0], (int)peak_db[1],
           (unsigned long)kws_audio_dropped());
}

/* ---------- screen ---------- */

static const ss_rgb kBg = {16, 20, 32}, kText = {235, 238, 245}, kMuted = {140, 150, 170},
                    kRec = {235, 80, 70}, kGood = {60, 200, 110}, kPanel = {28, 34, 52};

static void draw(void)
{
    char buf[64];
    ss_fill(0, 0, SS_LCD_W, SS_LCD_H, kBg);
    ss_text(SS_FONT_SMALL, 20, 20, "Sound Sense recorder", kText, 1);
    ss_text(SS_FONT_SMALL, 20, 56, "joystick centre: start / stop", kMuted, 1);
    const uint32_t s10 = (rec_takes ? rec_take_len[rec_takes - 1] : 0) / 6400u;   /* tenths of a second */
    snprintf(buf, sizeof(buf), "%s  take %lu", rec_state ? "RECORDING" : "idle", (unsigned long)rec_takes);
    ss_text(SS_FONT_SMALL, 20, 140, buf, rec_state ? kRec : kMuted, 2);
    snprintf(buf, sizeof(buf), "%lu:%02lu.%lu", (unsigned long)(s10 / 600), (unsigned long)(s10 / 10 % 60),
             (unsigned long)(s10 % 10));
    ss_text(SS_FONT_SMALL, 20, 230, buf, kText, 3);
    for (int c = 0; c < 2; c++) {
        const int y = 380 + c * 110;
        snprintf(buf, sizeof(buf), "mic %d: %d dBFS  (peak %d)", 4 + c, (int)lvl_db[c], (int)peak_db[c]);
        ss_text(SS_FONT_SMALL, 20, y, buf, kText, 1);
        ss_hbar(20, y + 40, 440, 30, (lvl_db[c] + 80.0f) / 80.0f, lvl_db[c] > -6.0f ? kRec : kGood, kPanel);
    }
    snprintf(buf, sizeof(buf), "dropped blocks: %lu", (unsigned long)kws_audio_dropped());
    ss_text(SS_FONT_SMALL, 20, 640, buf, kMuted, 1);
    ss_present();
}

int main(void)
{
    ss_board_init();
    ss_gfx_init();
    ss_joy_init();
    printf("\r\n[REC] Sound Sense recorder\r\n");
    const int pr = psram_init();
    if (pr != 0) {
        printf("[REC] PSRAM init failed (%d)\r\n", pr);
        ss_text(SS_FONT_SMALL, 20, 380, "PSRAM init failed", kRec, 1);
        ss_present();
        ss_halt("psram");
    }
    if (kws_audio_start() != 0) ss_halt("microphone start");
    printf("[REC] ready: PSRAM %u MB at 0x%08lx\r\n", (unsigned)(RAM_SIZE >> 20), (unsigned long)RAM_BASE);

    uint32_t last_draw = 0;
    for (;;) {
        kws_audio_poll();
        rec_dropped = kws_audio_dropped();
        const uint32_t cmd = rec_cmd;
        if (cmd) {
            rec_cmd = 0;
            if (cmd == 1 && !rec_state) start_take();
            else if (cmd == 2 && rec_state) stop_take();
            else if (cmd == 3 && !rec_state) rec_bytes = rec_takes = 0;
        }
        if (ss_joy_pressed() & SS_JOY_CENTER) {
            if (rec_state) stop_take();
            else start_take();
        }
        const uint32_t t = ss_ms();
        if (t - last_draw >= 200) {
            last_draw = t;
            draw();
        }
    }
}
