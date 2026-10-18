/* Sound Sense storage: settings and taught sounds in the 64 MB OSPI1 flash, so they survive power loss
 * (C2 in docs/HANDOFF-gates.md). Plain C.
 *
 * Two copies, A at flash offset 0x00100000 and B at 0x00110000, up to 64 KB each. A record is a
 * header (magic "SSND", version, sequence number, length, CRC32) and a payload (settings, then each
 * taught sound). A save erases and programs the older copy, then reads it back; booting loads the
 * valid copy with the highest sequence number. A save pulled half-way leaves the other copy intact.
 *
 * The CMSIS flash driver blocks for a whole 4 KB sector erase (about 82 ms), so a save runs in
 * steps: ss_store_step() does one sector erase, or the program and verify, per call. Call it from
 * the main loop between other work; the audio ring (320 ms) covers one step plus an inference. */
#pragma once
#include <stdint.h>

#include "assets/ss_encoder_io.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SS_STORE_MAX_PROF 8
#define SS_STORE_MAX_EX   10
#define SS_STORE_KEY      12

/* One taught sound. Examples are unit-norm embeddings scaled by 127 (ss_store_q8). */
typedef struct {
    char key[SS_STORE_KEY];   /* preset key, e.g. "doorbell" (looked up by name at load) */
    uint8_t icon, urgency, cat;   /* as shown; the category sets the LED colour */
    uint8_t n_ex, n_cx;       /* examples, counter-examples */
    float thr;
    float proto[SS_EMB];
    int8_t ex[SS_STORE_MAX_EX][SS_EMB];
    int8_t cx[SS_STORE_MAX_EX][SS_EMB];
} ss_store_prof;

typedef struct {
    uint32_t builtin_mute;    /* settings: bit i = built-in sound i doesn't alert */
    int n_prof;
    ss_store_prof prof[SS_STORE_MAX_PROF];
} ss_store_data;

typedef struct {
    uint32_t saves, failed;   /* finished saves, saves whose read-back didn't match */
    uint32_t seq;             /* sequence number of the newest valid copy */
    int copy;                 /* which copy that is: 0 A, 1 B, -1 none */
    uint32_t last_ms, max_ms; /* save request to verified, wall time */
    uint32_t last_len;        /* record bytes */
    uint32_t step_max_us;     /* longest single step (one blocking driver call sequence) */
    uint32_t load_us;         /* boot load time */
} ss_store_stats;

/* Pulse the flash reset and start the driver. 0 on success. */
int ss_store_init(void);

/* Load the newest valid copy into d. 0 on success, -1 if neither copy is valid. */
int ss_store_load(ss_store_data *d);

/* Snapshot d and start saving it. A save requested while one runs starts when that one ends. */
void ss_store_save(const ss_store_data *d);

int ss_store_busy(void);

/* One step of the running save. Returns 0 while more steps remain, 1 when the save finished and
 * verified, -1 when it finished and failed (the other copy is still valid). */
int ss_store_step(void);

const ss_store_stats *ss_store_get_stats(void);

/* Unit-norm float embedding to int8 (x 127) and back (not re-normalised). */
void ss_store_q8(const float *e, int8_t *q);
void ss_store_f32(const int8_t *q, float *e);

#ifdef __cplusplus
}
#endif
