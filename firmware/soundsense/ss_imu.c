/* Sound Sense vibration sensing (see ss_imu.h). */
#include "ss_imu.h"

#include <math.h>
#include <string.h>

#include "Driver_IMU.h"
#include "ss_board.h"
#include "vendor/ss_bmi323.h"

extern ARM_DRIVER_IMU BMI323;

#define HP_FC_HZ       10.0f
#define NOISE_TAU_S    0.5f
#define PEAK_WIN_MS    20
#define MAX_FRAMES     64
#define EVQ            16
#define TIME_PER_FRAME (SS_IMU_TIME_HZ / SS_IMU_ODR_HZ)   /* 32 sensor-time ticks at 800 Hz */

static ss_imu_stats st;
static ss_imu_tap_fn tap;
static bool started, have_time, have_anchor, have_cyc;
static uint16_t last_time;
static uint32_t ext_time;                 /* sensor time unwrapped, in ticks of 1/25600 s */
static uint32_t anchor_ext, anchor_cyc;   /* a frame's sensor time and its DWT time */
static uint32_t base_ext, base_cyc;       /* an older anchor, to measure the sensor clock against DWT */
static float cyc_per_tick;                /* DWT cycles per sensor-time tick: nominal 15625, but the sensor runs fast */
static uint32_t last_cyc;
volatile uint32_t ss_imu_old_timing;
static float lp[3], hp_a, noise_a;
static uint32_t warmup;            /* frames before the noise level is trusted */
static uint32_t refract;           /* frames left in the refractory period */
static int pending_left = -1;      /* frames left in the peak window of the pending event */
static ss_knock pending;
static ss_knock evq[EVQ];
static uint32_t ev_w, ev_r;

int ss_imu_init(void)
{
    if (BMI323.Initialize() != ARM_DRIVER_OK || BMI323.PowerControl(ARM_POWER_FULL) != ARM_DRIVER_OK) return -1;
    /* the driver leaves its data-ready interrupt armed; we poll the FIFO instead */
    BMI323.Control(IMU_SET_INTERRUPT, 0);
    if (BMI323_FifoStart() != ARM_DRIVER_OK) return -2;
    const float pi = 3.14159265f;
    hp_a = 1.0f - expf(-2.0f * pi * HP_FC_HZ / SS_IMU_ODR_HZ);
    noise_a = 1.0f / (NOISE_TAU_S * SS_IMU_ODR_HZ);
    memset(&st, 0, sizeof(st));
    st.noise_mg = 1.0f;
    warmup = SS_IMU_ODR_HZ / 2;
    started = true;
    return 0;
}

void ss_imu_set_tap(ss_imu_tap_fn fn) { tap = fn; }
const ss_imu_stats *ss_imu_get_stats(void) { return &st; }

void ss_imu_reset_stats(void)
{
    const float noise = st.noise_mg;
    memset(&st, 0, sizeof(st));
    st.noise_mg = noise;
    have_time = have_anchor = have_cyc = false;
}

/* New anchor; every 5 s of sensor time, re-measure the sensor clock against the DWT clock (the anchors'
 * jitter of under one frame is 0.03% over 5 s; DWT wraps after 10.7 s at 400 MHz). */
static void set_anchor(uint32_t ext, uint32_t cyc)
{
    if (!have_anchor) {
        base_ext = ext, base_cyc = cyc;
        cyc_per_tick = (float)SystemCoreClock / SS_IMU_TIME_HZ;
    } else if (ext - base_ext >= 5u * SS_IMU_TIME_HZ) {
        cyc_per_tick = (float)(cyc - base_cyc) / (float)(ext - base_ext);
        st.clock_ppm = (int32_t)(1e6f * ((float)SystemCoreClock / SS_IMU_TIME_HZ / cyc_per_tick - 1.0f));
        base_ext = ext, base_cyc = cyc;
    }
    anchor_ext = ext, anchor_cyc = cyc;
    have_anchor = true;
}

static void detect(const int16_t *xyz, uint32_t cyc)
{
    float m2 = 0.0f;
    for (int a = 0; a < 3; a++) {
        const float g = xyz[a] * (1000.0f / SS_IMU_LSB_PER_G);   /* mg */
        st.acc_mg[a] = g;
        if (warmup == SS_IMU_ODR_HZ / 2 && st.samples == 1) lp[a] = g;   /* start the filter at rest */
        lp[a] += hp_a * (g - lp[a]);
        const float h = g - lp[a];
        m2 += h * h;
    }
    const float m = sqrtf(m2);

    if (pending_left >= 0) {
        if (m > pending.peak_mg) pending.peak_mg = m;
        if (pending_left-- == 0 && ev_w - ev_r < EVQ) evq[ev_w++ % EVQ] = pending;
    }
    if (warmup) {
        warmup--;
        st.noise_mg += 4.0f * noise_a * (m - st.noise_mg);   /* settle quickly at start */
        return;
    }
    if (refract) {
        refract--;
        return;   /* the knock's own ringing doesn't raise the noise level */
    }
    if (m > SS_KNOCK_RATIO * st.noise_mg && m > SS_KNOCK_MIN_MG) {
        pending.cyc = cyc;
        pending.peak_mg = m;
        pending.noise_mg = st.noise_mg;
        pending_left = PEAK_WIN_MS * SS_IMU_ODR_HZ / 1000;
        refract = SS_KNOCK_REFRACT_MS * SS_IMU_ODR_HZ / 1000;
        return;
    }
    st.noise_mg += noise_a * (m - st.noise_mg);
}

int ss_imu_poll(void)
{
    static int16_t fr[MAX_FRAMES * SS_IMU_FRAME_WORDS];
    if (!started) return -1;
    const uint32_t t0 = ss_cycles();
    uint32_t fill = 0;
    const int32_t n = BMI323_FifoRead(fr, MAX_FRAMES, &fill);
    const uint32_t t1 = ss_cycles();
    if (n < 0) {
        st.errors++;
        return (int)n;
    }
    st.reads++;
    st.read_cyc += t1 - t0;
    if (t1 - t0 > st.read_cyc_max) st.read_cyc_max = t1 - t0;
    if (fill > st.fill_max) st.fill_max = fill;

    /* Frame times come from the sensor's own clock (16-bit sensor time in each frame, unwrapped), tied
     * to the DWT clock by an anchor: when a read empties the FIFO, its newest frame was sampled within
     * one period before the fill level was read (t0). A read that leaves frames behind (after a long
     * stall: more than MAX_FRAMES waiting) is timed from the last anchor, so times never jump back. */
    const uint32_t per = SystemCoreClock / SS_IMU_ODR_HZ;
    const bool drained = (uint32_t)n == fill / SS_IMU_FRAME_WORDS;
    uint32_t ext0 = have_time ? ext_time : (uint16_t)fr[3];   /* unwrapped sensor time of the newest frame */
    uint16_t prev = have_time ? last_time : (uint16_t)fr[3];
    for (int32_t k = 0; k < n; k++) {
        const uint16_t t = (uint16_t)fr[k * SS_IMU_FRAME_WORDS + 3];
        ext0 += (uint16_t)(t - prev);
        prev = t;
    }
    if (n > 0 && drained) set_anchor(ext0, t0 - per / 2);
    for (int32_t k = 0; k < n; k++) {
        const int16_t *f = &fr[k * SS_IMU_FRAME_WORDS];
        const uint16_t t = (uint16_t)f[3];
        if (have_time) {
            const uint16_t d = (uint16_t)(t - last_time);
            if (d > TIME_PER_FRAME + TIME_PER_FRAME / 2) st.missed += (d + TIME_PER_FRAME / 2) / TIME_PER_FRAME - 1;
            ext_time += d;
        } else {
            ext_time = t;
        }
        have_time = true;
        last_time = t;
        st.samples++;
        const int32_t dt = (int32_t)(ext_time - anchor_ext);   /* ticks after the anchor frame */
        uint32_t cyc;
        if (ss_imu_old_timing || !have_anchor || dt > 100000 || dt < -100000)
            cyc = t0 - (uint32_t)(n - 1 - k) * per;
        else
            cyc = anchor_cyc + (uint32_t)(int32_t)((float)dt * cyc_per_tick);
        if (have_cyc && (int32_t)(cyc - last_cyc) < 0) {
            const uint32_t back = ss_cyc_to_us(last_cyc - cyc);
            if (back > st.back_max_us) st.back_max_us = back;
        }
        have_cyc = true;
        last_cyc = cyc;
        detect(f, cyc);
        if (tap) tap(f, t, cyc);
    }
    return (int)n;
}

int ss_imu_knock(ss_knock *k)
{
    if (ev_r == ev_w) return 0;
    *k = evq[ev_r++ % EVQ];
    return 1;
}

void ss_imu_measure_polling(uint32_t ms, uint32_t *reads, uint32_t *distinct, uint32_t *us_per_read)
{
    uint16_t w[9], prev[3] = {0};   /* ACC_DATA_X 0x03 .. SENSOR_TIME_1 0x0B */
    uint32_t n = 0, d = 0, busy = 0;
    const uint32_t t_end = ss_ms() + ms;
    while (ss_ms() < t_end) {
        const uint32_t t0 = ss_cycles();
        if (BMI323_ReadWords(0x03, w, 9) != 0) break;
        busy += ss_cycles() - t0;
        n++;
        /* a new sample changes at least one axis (sensor noise is several LSB at +-4 g) */
        if (n == 1 || w[0] != prev[0] || w[1] != prev[1] || w[2] != prev[2]) d++;
        memcpy(prev, w, sizeof(prev));
    }
    *reads = n;
    *distinct = d;
    *us_per_read = n ? ss_cyc_to_us(busy / n) : 0;
}
