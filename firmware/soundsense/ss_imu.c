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
static bool started, have_time;
static uint16_t last_time;
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
    have_time = false;
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

    /* The newest frame was sampled within one period before the read started. */
    const uint32_t per = SystemCoreClock / SS_IMU_ODR_HZ;
    for (int32_t k = 0; k < n; k++) {
        const int16_t *f = &fr[k * SS_IMU_FRAME_WORDS];
        const uint16_t t = (uint16_t)f[3];
        if (have_time) {
            const uint16_t d = (uint16_t)(t - last_time);
            if (d > TIME_PER_FRAME + TIME_PER_FRAME / 2) st.missed += (d + TIME_PER_FRAME / 2) / TIME_PER_FRAME - 1;
        }
        have_time = true;
        last_time = t;
        st.samples++;
        const uint32_t cyc = t0 - (uint32_t)(n - 1 - k) * per;
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
