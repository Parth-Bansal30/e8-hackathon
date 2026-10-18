/* Sound Sense vibration sensing: the BMI323 accelerometer streamed through its FIFO at 800 Hz
 * (+-4 g), and a knock detector on the high-passed acceleration. Plain C.
 *
 * Detector: each axis is high-passed (one pole at 10 Hz, removes gravity and tilt), the magnitude of
 * the high-passed vector is compared with a running noise level (mean over quiet samples), and a
 * knock is flagged when it exceeds SS_KNOCK_RATIO times that level (and SS_KNOCK_MIN_MG), followed
 * by an 80 ms refractory period. Times are DWT cycle counts, so they compare with ss_audio's. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SS_KNOCK_RATIO      6.0f
#define SS_KNOCK_MIN_MG     1.5f
#define SS_KNOCK_REFRACT_MS 80

typedef struct {
    uint32_t cyc;        /* estimated DWT time of the first sample over the threshold */
    float peak_mg;       /* largest high-passed magnitude within 20 ms of the trigger */
    float noise_mg;      /* noise level when it triggered */
} ss_knock;

typedef struct {
    uint32_t samples;        /* frames read since ss_imu_init */
    uint32_t missed;         /* frames lost, from gaps in the sensor time */
    uint32_t reads;          /* FIFO reads */
    uint32_t errors;         /* failed I3C transfers */
    uint32_t read_cyc;       /* total cycles spent reading the FIFO */
    uint32_t read_cyc_max;
    uint32_t fill_max;       /* highest FIFO fill level seen, in words */
    uint32_t back_max_us;    /* largest step backwards between consecutive frame times (should be ~0) */
    int32_t clock_ppm;       /* sensor clock vs the CPU clock, parts per million (+ = sensor fast) */
    float noise_mg;          /* current noise level of the high-passed magnitude */
    float acc_mg[3];         /* latest raw acceleration */
} ss_imu_stats;

/* Start the I3C bus, the BMI323 (800 Hz, +-4 g, high performance) and its FIFO. 0 on success. */
int ss_imu_init(void);

/* 1: time frames the old way (newest frame of each read = the read time), to compare; set over J-Link. */
extern volatile uint32_t ss_imu_old_timing;

/* Read whatever the FIFO holds and run the detector on it. Call every few ms (the FIFO holds
 * about 300 ms). Returns the number of new frames, or < 0 on an I3C error. */
int ss_imu_poll(void);

/* Pop the next finished knock event (it is finished 20 ms after the trigger). 1 if one was returned. */
int ss_imu_knock(ss_knock *k);

const ss_imu_stats *ss_imu_get_stats(void);
void ss_imu_reset_stats(void);

/* Optional raw tap for test benches: called for every frame with its axes (raw LSB at 8192 LSB/g),
 * the 16-bit sensor time and the estimated DWT time. */
typedef void (*ss_imu_tap_fn)(const int16_t *xyz, uint16_t sensor_time, uint32_t cyc);
void ss_imu_set_tap(ss_imu_tap_fn fn);

/* Polling alternative, for comparison: read the data registers (acc + sensor time) as fast as possible
 * for ms milliseconds. Reports the time per read and how many distinct samples were seen. */
void ss_imu_measure_polling(uint32_t ms, uint32_t *reads, uint32_t *distinct, uint32_t *us_per_read);

#ifdef __cplusplus
}
#endif
