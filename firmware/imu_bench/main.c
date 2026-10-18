/* Sound Sense IMU bench (docs/HANDOFF-gates.md G2): can the BMI323 feel a knock, and does the felt
 * knock line up with the heard one?
 *
 * Runs the product modules: ss_imu (800 Hz FIFO, knock detector) and ss_audio (sample-accurate audio
 * onsets from ../app/kws.c). Pairs each audio onset with an IMU knock within +-50 ms:
 *   felt + heard (LED0 green), heard only (LED0 blue), felt only (LED1 red).
 * Every event is logged to RTT with the audio-minus-IMU time difference; stats every 5 s.
 *
 * Control: joystick centre starts a new trial (counters reset). Over J-Link (tools/jlink.py): write
 * bench_arg, then bench_cmd = 1 new trial (id = bench_arg), 2 LED check (C3), 3 polling measurement.
 * The last 40 s of mic 4 and of the accelerometer are kept in SRAM1 (bench_audio, bench_imu) so a
 * trial can be re-analysed on the host (tools/g2_offline.py). */
#include "RTE_Components.h"
#include CMSIS_device_header

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "kws.h"
#include "ss_audio.h"
#include "ss_board.h"
#include "ss_gfx.h"
#include "ss_imu.h"
#include "vendor/ss_bmi323.h"

#define PAIR_WIN_MS   50
#define SETTLE_MS     150     /* wait this long before deciding an event has no partner */
#define STATS_MS      5000
#define MAXEV         32

/* ---------- host interface ---------- */
volatile uint32_t bench_cmd, bench_arg, bench_trial;
volatile uint32_t bench_both, bench_heard, bench_felt;   /* counts in the current trial */

/* ---------- raw rings for offline analysis ---------- */
#define RAW_AUDIO_N (40u * 16000u)
#define RAW_IMU_N   (40u * 800u)
typedef struct { int16_t x, y, z; uint16_t t; uint32_t cyc; } imu_rec;
int16_t bench_audio[RAW_AUDIO_N] __attribute__((section(".bss.at_sram1"), aligned(16)));
uint32_t bench_audio_blkcyc[RAW_AUDIO_N / KWS_HOP] __attribute__((section(".bss.at_sram1")));
imu_rec bench_imu[RAW_IMU_N] __attribute__((section(".bss.at_sram1"), aligned(16)));
volatile uint32_t bench_audio_n, bench_imu_n;

static void audio_tap(const int16_t *stereo, int n, uint32_t done_cycles)
{
    const uint32_t base = bench_audio_n;
    for (int i = 0; i < n; i++) bench_audio[(base + i) % RAW_AUDIO_N] = stereo[2 * i];
    bench_audio_blkcyc[(base / KWS_HOP) % (RAW_AUDIO_N / KWS_HOP)] = done_cycles;
    bench_audio_n = base + (uint32_t)n;
}

static void imu_tap(const int16_t *xyz, uint16_t t, uint32_t cyc)
{
    imu_rec *r = &bench_imu[bench_imu_n % RAW_IMU_N];
    r->x = xyz[0];
    r->y = xyz[1];
    r->z = xyz[2];
    r->t = t;
    r->cyc = cyc;
    bench_imu_n++;
}

/* ---------- event pairing ---------- */
typedef struct { uint32_t cyc; float a, b; bool used; } ev;
static ev aud[MAXEV], imu[MAXEV];
static uint32_t n_aud, n_imu;
static float dt_ms[256];
static uint32_t n_dt;
static char last_msg[64] = "waiting for a knock";

static void led_flash(int led, uint32_t colour);

static int32_t ms_between(uint32_t later, uint32_t earlier)
{
    return (int32_t)(later - earlier) / (int32_t)(SystemCoreClock / 1000u);
}

static void pair_events(void)
{
    const uint32_t now = ss_cycles();
    for (uint32_t i = 0; i < MAXEV; i++) {
        ev *a = &aud[i];
        if (a->used || !a->cyc || ms_between(now, a->cyc) < SETTLE_MS) continue;
        /* closest unused IMU event within the window */
        int best = -1;
        float best_dt = 1e9f;
        for (int j = 0; j < MAXEV; j++) {
            if (imu[j].used || !imu[j].cyc) continue;
            const float d = (float)(int32_t)(a->cyc - imu[j].cyc) / (SystemCoreClock / 1000.0f);
            if (fabsf(d) <= PAIR_WIN_MS && fabsf(d) < fabsf(best_dt)) { best = j; best_dt = d; }
        }
        a->used = true;
        if (best >= 0) {
            imu[best].used = true;
            bench_both++;
            if (n_dt < 256) dt_ms[n_dt++] = best_dt;
            printf("[G2] trial %lu FELT+HEARD dt=%+.1f ms  imu %.1f mg (noise %.2f)  audio peak %.0f dBFS\r\n",
                   (unsigned long)bench_trial, (double)best_dt, (double)imu[best].a, (double)imu[best].b,
                   (double)(20.0f * log10f(a->a + 1e-9f)));
            snprintf(last_msg, sizeof(last_msg), "FELT + HEARD  dt %+.1f ms", (double)best_dt);
            led_flash(0, SS_LED_GREEN);
        } else {
            bench_heard++;
            printf("[G2] trial %lu HEARD ONLY  audio peak %.0f dBFS (floor %.0f dBFS)\r\n",
                   (unsigned long)bench_trial, (double)(20.0f * log10f(a->a + 1e-9f)),
                   (double)(20.0f * log10f(a->b + 1e-9f)));
            snprintf(last_msg, sizeof(last_msg), "heard only");
            led_flash(0, SS_LED_BLUE);
        }
    }
    for (uint32_t j = 0; j < MAXEV; j++) {
        ev *m = &imu[j];
        if (m->used || !m->cyc || ms_between(now, m->cyc) < SETTLE_MS + PAIR_WIN_MS) continue;
        m->used = true;
        bench_felt++;
        printf("[G2] trial %lu FELT ONLY  imu %.1f mg (noise %.2f)\r\n", (unsigned long)bench_trial,
               (double)m->a, (double)m->b);
        snprintf(last_msg, sizeof(last_msg), "felt only  %.1f mg", (double)m->a);
        led_flash(1, SS_LED_RED);
    }
}

static void new_trial(uint32_t id)
{
    if (n_dt) {
        /* summary of the trial that just ended */
        float s[256];
        memcpy(s, dt_ms, n_dt * sizeof(float));
        for (uint32_t i = 1; i < n_dt; i++)
            for (uint32_t k = i; k > 0 && s[k] < s[k - 1]; k--) { float t = s[k]; s[k] = s[k - 1]; s[k - 1] = t; }
        uint32_t within = 0;
        for (uint32_t i = 0; i < n_dt; i++) within += fabsf(s[i] - s[n_dt / 2]) <= 10.0f;
        printf("[G2] trial %lu summary: felt+heard %lu, heard only %lu, felt only %lu; dt median %+.1f ms, "
               "min %+.1f, max %+.1f, %lu/%lu within +-10 ms of the median\r\n",
               (unsigned long)bench_trial, (unsigned long)bench_both, (unsigned long)bench_heard,
               (unsigned long)bench_felt, (double)s[n_dt / 2], (double)s[0], (double)s[n_dt - 1],
               (unsigned long)within, (unsigned long)n_dt);
    } else {
        printf("[G2] trial %lu summary: felt+heard %lu, heard only %lu, felt only %lu\r\n", (unsigned long)bench_trial,
               (unsigned long)bench_both, (unsigned long)bench_heard, (unsigned long)bench_felt);
    }
    bench_trial = id;
    bench_both = bench_heard = bench_felt = 0;
    n_dt = 0;
    memset(aud, 0, sizeof(aud));
    memset(imu, 0, sizeof(imu));
    snprintf(last_msg, sizeof(last_msg), "trial %lu: waiting", (unsigned long)id);
    printf("[G2] trial %lu started at audio sample %lu, imu frame %lu\r\n", (unsigned long)id,
           (unsigned long)bench_audio_n, (unsigned long)bench_imu_n);
}

/* ---------- LEDs: event flashes and the C3 check sequence ---------- */
static uint32_t led_off_at[2];
static int led_check_step = -1;
static uint32_t led_check_t;
static char led_msg[48] = "";

static void led_flash(int led, uint32_t colour)
{
    if (led_check_step >= 0) return;
    ss_led_set(led, colour);
    led_off_at[led] = ss_ms() + 250;
}

/* steps 0-7: each LED in red, green, blue, white for 2 s; 8-10: both blinking white at 2, 4, 8 Hz for 4 s */
static void led_check_run(void)
{
    static const char *const cname[4] = {"red", "green", "blue", "white"};
    static const uint32_t cbits[4] = {SS_LED_RED, SS_LED_GREEN, SS_LED_BLUE, SS_LED_WHITE};
    if (led_check_step < 0) return;
    const uint32_t t = ss_ms() - led_check_t;
    if (led_check_step < 8) {
        const int led = led_check_step / 4, c = led_check_step % 4;
        ss_led_set(led, cbits[c]);
        ss_led_set(1 - led, 0);
        snprintf(led_msg, sizeof(led_msg), "LED%d %s", led, cname[c]);
        if (t >= 2000) {
            printf("[C3] shown: LED%d %s\r\n", led, cname[c]);
            led_check_step++;
            led_check_t = ss_ms();
        }
    } else if (led_check_step < 11) {
        static const uint32_t hz[3] = {2, 4, 8};
        const uint32_t f = hz[led_check_step - 8], on = (t * f * 2 / 1000) % 2 == 0;
        ss_led_set(0, on ? SS_LED_WHITE : 0);
        ss_led_set(1, on ? SS_LED_WHITE : 0);
        snprintf(led_msg, sizeof(led_msg), "both blinking %lu Hz", (unsigned long)f);
        if (t >= 4000) {
            printf("[C3] shown: blink %lu Hz\r\n", (unsigned long)f);
            led_check_step++;
            led_check_t = ss_ms();
        }
    } else {
        ss_led_set(0, 0);
        ss_led_set(1, 0);
        led_msg[0] = 0;
        led_check_step = -1;
        printf("[C3] LED check done\r\n");
    }
}

/* ---------- screen ---------- */
static const ss_rgb kBg = {16, 20, 32}, kText = {235, 238, 245}, kMuted = {140, 150, 170},
                    kGood = {60, 200, 110}, kWarn = {240, 190, 60}, kPanel = {28, 34, 52};
static char stats_line[3][64];

/* The person's script: joystick centre moves to the next trial (shown on screen). */
static const char *const trial_name[] = {
    "warm-up (press centre to start)", "20 knocks, 20 cm away", "20 knocks, 50 cm away", "20 knocks, 100 cm away",
    "phone 30 cm above table: knock sounds", "phone lying on table: knock sounds", "typing on this table, 60 s",
    "cup put down 10 times", "walk past 5 times", "done: thank you",
};
#define N_TRIAL_NAMES (sizeof(trial_name) / sizeof(trial_name[0]))

static void draw(void)
{
    char buf[64];
    ss_fill(0, 0, SS_LCD_W, SS_LCD_H, kBg);
    ss_text(SS_FONT_SMALL, 20, 16, "Sound Sense: knock bench (G2)", kText, 1);
    snprintf(buf, sizeof(buf), "trial %lu", (unsigned long)bench_trial);
    ss_text(SS_FONT_SMALL, 20, 56, buf, kText, 2);
    ss_text(SS_FONT_SMALL, 20, 112, bench_trial < N_TRIAL_NAMES ? trial_name[bench_trial] : "", kWarn, 1);
    snprintf(buf, sizeof(buf), "felt+heard %lu", (unsigned long)bench_both);
    ss_text(SS_FONT_SMALL, 20, 160, buf, kGood, 2);
    snprintf(buf, sizeof(buf), "heard only %lu", (unsigned long)bench_heard);
    ss_text(SS_FONT_SMALL, 20, 225, buf, kText, 2);
    snprintf(buf, sizeof(buf), "felt only  %lu", (unsigned long)bench_felt);
    ss_text(SS_FONT_SMALL, 20, 290, buf, kWarn, 2);
    ss_text(SS_FONT_SMALL, 20, 380, last_msg, kText, 1);
    const ss_imu_stats *s = ss_imu_get_stats();
    snprintf(buf, sizeof(buf), "vib %.2f mg  room %.0f dBFS", (double)s->noise_mg, (double)ss_audio_floor_db());
    ss_text(SS_FONT_SMALL, 20, 430, buf, kMuted, 1);
    ss_hbar(20, 470, 440, 16, (ss_audio_block_db() + 80.0f) / 80.0f, kGood, kPanel);
    for (int i = 0; i < 3; i++) ss_text(SS_FONT_SMALL, 20, 520 + 34 * i, stats_line[i], kMuted, 1);
    if (led_msg[0]) ss_text(SS_FONT_SMALL, 20, 660, led_msg, kWarn, 3);
    ss_present();
}

int main(void)
{
    ss_board_init();
    ss_gfx_init();
    ss_joy_init();
    if (ss_led_init() != 0) printf("[G2] LED init failed\r\n");
    printf("\r\n[G2] Sound Sense knock bench, IMU at %d Hz\r\n", SS_IMU_ODR_HZ);
    const int ir = ss_imu_init();
    if (ir != 0) {
        printf("[G2] IMU init failed (%d)\r\n", ir);
        ss_halt("imu");
    }
    uint32_t reads, distinct, us;
    ss_imu_measure_polling(1000, &reads, &distinct, &us);
    printf("[G2] polling: %lu register reads in 1 s, %lu distinct samples, %lu us per read\r\n",
           (unsigned long)reads, (unsigned long)distinct, (unsigned long)us);
    ss_imu_init();   /* restart the FIFO after the polling test */
    ss_imu_set_tap(imu_tap);
    ss_audio_init();
    ss_audio_set_tap(audio_tap);
    if (kws_audio_start() != 0) ss_halt("microphone start");
    printf("[G2] listening\r\n");
    led_check_step = 0;   /* C3: run the LED check once at boot */
    led_check_t = ss_ms();
    printf("[C3] LED check start\r\n");

    uint32_t last_draw = 0, last_stats = ss_ms(), last_imu = 0;
    uint32_t stat_cyc0 = ss_cycles(), stat_samples0 = 0, stat_read0 = 0, stat_missed0 = 0;
    for (;;) {
        kws_audio_poll();
        const uint32_t t = ss_ms();
        if (t - last_imu >= 5) {
            last_imu = t;
            ss_imu_poll();
        }
        ss_onset o;
        while (ss_audio_onset(&o)) {
            for (int i = 0; i < MAXEV; i++) {
                if (!aud[i].cyc || aud[i].used) {
                    aud[i] = (ev){o.cyc, o.peak, o.floor, false};
                    break;
                }
            }
        }
        ss_knock k;
        while (ss_imu_knock(&k)) {
            for (int i = 0; i < MAXEV; i++) {
                if (!imu[i].cyc || imu[i].used) {
                    imu[i] = (ev){k.cyc, k.peak_mg, k.noise_mg, false};
                    break;
                }
            }
        }
        pair_events();

        for (int l = 0; l < 2; l++) {
            if (led_off_at[l] && (int32_t)(t - led_off_at[l]) >= 0 && led_check_step < 0) {
                ss_led_set(l, 0);
                led_off_at[l] = 0;
            }
        }
        led_check_run();

        const uint32_t cmd = bench_cmd;
        if (cmd) {
            bench_cmd = 0;
            if (cmd == 1) new_trial(bench_arg);
            else if (cmd == 2) { led_check_step = 0; led_check_t = ss_ms(); printf("[C3] LED check start\r\n"); }
            else if (cmd == 3) {
                ss_imu_measure_polling(1000, &reads, &distinct, &us);
                printf("[G2] polling: %lu reads, %lu distinct, %lu us/read\r\n", (unsigned long)reads,
                       (unsigned long)distinct, (unsigned long)us);
                ss_imu_init();
            }
        }
        const uint32_t joy = ss_joy_pressed();
        if (joy) {
            static const char *const nm[5] = {"A", "B", "C", "D", "centre"};
            for (int i = 0; i < 5; i++) if (joy & (1u << i)) printf("[G2] joystick %s\r\n", nm[i]);
            if (joy & SS_JOY_CENTER) {
                new_trial(bench_trial + 1);
                if (bench_trial < N_TRIAL_NAMES) printf("[G2] trial %lu is: %s\r\n", (unsigned long)bench_trial, trial_name[bench_trial]);
            }
        }

        if (t - last_stats >= STATS_MS) {
            const ss_imu_stats *s = ss_imu_get_stats();
            const uint32_t cyc = ss_cycles() - stat_cyc0;
            const float secs = cyc / (float)SystemCoreClock;
            const float rate = (s->samples - stat_samples0) / secs;
            const float cpu = 100.0f * (s->read_cyc - stat_read0) / (float)cyc;
            const uint32_t nreads = s->reads ? s->reads : 1;
            printf("[G2] stats: imu %.1f Hz, missed %lu (+%lu), fifo max %lu words, read avg %lu us max %lu us, "
                   "imu cpu %.2f%%, i3c errors %lu, audio dropped %lu, vib noise %.2f mg, room %.0f dBFS, "
                   "acc %.0f %.0f %.0f mg\r\n",
                   (double)rate, (unsigned long)s->missed, (unsigned long)(s->missed - stat_missed0),
                   (unsigned long)s->fill_max, (unsigned long)ss_cyc_to_us(s->read_cyc / nreads),
                   (unsigned long)ss_cyc_to_us(s->read_cyc_max), (double)cpu, (unsigned long)s->errors,
                   (unsigned long)kws_audio_dropped(), (double)s->noise_mg, (double)ss_audio_floor_db(),
                   (double)s->acc_mg[0], (double)s->acc_mg[1], (double)s->acc_mg[2]);
            snprintf(stats_line[0], sizeof(stats_line[0]), "imu %.1f Hz  missed %lu", (double)rate,
                     (unsigned long)s->missed);
            snprintf(stats_line[1], sizeof(stats_line[1]), "read %lu us  cpu %.2f%%",
                     (unsigned long)ss_cyc_to_us(s->read_cyc / nreads), (double)cpu);
            snprintf(stats_line[2], sizeof(stats_line[2]), "audio dropped %lu", (unsigned long)kws_audio_dropped());
            last_stats = t;
            stat_cyc0 = ss_cycles();
            stat_samples0 = s->samples;
            stat_read0 = s->read_cyc;
            stat_missed0 = s->missed;
        }
        if (t - last_draw >= 200) {
            last_draw = t;
            draw();
        }
    }
}
