/* Keyword spotting front end: 16 kHz PDM capture and log-mel features.
 * The features match model/kws_common.py exactly; kws_tables.h is generated from it. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "kws_tables.h"

/* Start the PDM mics at 16 kHz. Returns 0 on success. */
int kws_audio_start(void);

/* Process captured audio: one log-mel frame per 20 ms block. Returns the number of new
 * frames since the last call (0 if none). Call often from the main loop. */
int kws_audio_poll(void);

/* Level of the latest 20 ms block, in dBFS. */
float kws_audio_level_db(void);

/* Blocks the capture had to drop because the main loop fell behind. */
uint32_t kws_audio_dropped(void);

/* Build the model input from the latest 49 frames (about 1 s of audio): subtract the
 * patch mean, scale, then quantize with the model's int8 input parameters. */
void kws_make_input(int8_t *out, float in_scale, int in_zp);

/* Feature self-test: run the C pipeline on a 1 s clip and quantize it like kws_make_input. */
void kws_features_of_clip(const int16_t *clip, int8_t *out, float in_scale, int in_zp);

/* Optional raw-audio tap. kws_audio_poll() calls it for every captured block before computing that
 * block's features: 'stereo' holds n time steps of interleaved samples (mic 4, mic 5), and
 * done_cycles is the DWT cycle count when the block completed. The default does nothing; an app
 * that needs raw audio (recorder, onset timing) defines its own. */
void kws_block_hook(const int16_t *stereo, int n, uint32_t done_cycles);

#ifdef __cplusplus
}
#endif
