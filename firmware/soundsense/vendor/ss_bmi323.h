/* Sound Sense additions to the vendored BMI323 driver (BMI323_IMU.c in this folder). */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Accelerometer output rate: ACC_CONF odr code 0xB = 800 Hz, 0xC = 1600 Hz (bmi3_defs.h). */
#ifndef SS_IMU_ODR_CODE
#define SS_IMU_ODR_CODE 0xB
#endif
#define SS_IMU_ODR_HZ      (SS_IMU_ODR_CODE == 0xC ? 1600 : 800)
#define SS_IMU_LSB_PER_G   8192          /* +-4 g */
#define SS_IMU_FRAME_WORDS 4             /* FIFO frame: acc x, y, z, then sensor time bits 15:0 */
#define SS_IMU_TIME_HZ     25600         /* sensor time: 39.0625 us per LSB */

/* Read n 16-bit registers starting at reg (the 2 dummy bytes are stripped). */
int32_t BMI323_ReadWords(uint8_t reg, uint16_t *words, uint32_t n_words);
int32_t BMI323_WriteWord(uint8_t reg, uint16_t value);

/* Put the accelerometer and the sensor time into the FIFO and flush it. Call after
 * BMI323.PowerControl(ARM_POWER_FULL). */
int32_t BMI323_FifoStart(void);

/* Read up to max_frames complete frames (SS_IMU_FRAME_WORDS words each) from the FIFO. Returns the
 * number read, or a negative ARM_DRIVER_ error. fill_words gets the fill level seen before reading. */
int32_t BMI323_FifoRead(int16_t *frames, uint32_t max_frames, uint32_t *fill_words);

#ifdef __cplusplus
}
#endif
