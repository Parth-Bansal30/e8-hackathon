/* Sound Sense raw-audio tap on top of ../app/kws.c: keeps the last 2 s of mic 4 with the time of
 * every sample, tracks the room's noise floor, and finds sound onsets at sample resolution.
 * It implements kws_block_hook, so it is fed by kws_audio_poll(). Plain C.
 *
 * Onset: the first sample in a block whose magnitude exceeds SS_ONSET_RATIO times the noise floor
 * (RMS of the background), then SS_ONSET_REFRACT_MS without new onsets. */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SS_AUDIO_RING       32000   /* samples: 2 s at 16 kHz */
#define SS_ONSET_RATIO      6.0f
#define SS_ONSET_REFRACT_MS 100

typedef struct {
    uint32_t sample;     /* index of the onset sample (counts every sample since start) */
    uint32_t cyc;        /* its DWT time */
    float peak;          /* largest |x| in the onset's block, full scale = 1 */
    float floor;         /* noise floor (RMS, full scale = 1) when it fired */
} ss_onset;

void ss_audio_init(void);   /* call before kws_audio_start() */

int ss_audio_onset(ss_onset *o);       /* pop the next onset; 1 if one was returned */
uint32_t ss_audio_samples(void);       /* samples captured so far = index of the next sample */
float ss_audio_floor_db(void);         /* noise floor as an RMS level, dBFS */
float ss_audio_block_db(void);         /* level of the latest 20 ms block, dBFS */
uint32_t ss_audio_sample_cyc(uint32_t sample);   /* DWT time of a sample still in the ring */

/* Copy n samples starting at 'first' into out (int16). Returns 0, or -1 if they aren't all in the
 * ring yet (or any more). */
int ss_audio_copy(uint32_t first, int16_t *out, uint32_t n);

/* Optional extra consumer of the raw stereo blocks (test benches). */
typedef void (*ss_audio_tap_fn)(const int16_t *stereo, int n, uint32_t done_cycles);
void ss_audio_set_tap(ss_audio_tap_fn fn);

#ifdef __cplusplus
}
#endif
