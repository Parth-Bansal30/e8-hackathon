/* Sound Sense matching: encoder output to a unit-norm embedding, cosine scores against sound
 * profiles, and new profiles from a few examples. Same rules as model/ss_common.py: a profile is the
 * unit-norm mean of its examples' unit-norm embeddings; its threshold is the lowest leave-one-out
 * similarity minus 0.05, clamped to [0.5, 0.85]. Plain C. */
#pragma once
#include <stdint.h>

#include "assets/ss_encoder_io.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SS_THR_MARGIN 0.05f
#define SS_THR_MIN    0.50f
#define SS_THR_MAX    0.85f

/* Dequantize the int8 encoder output and scale it to unit length. */
void ss_embed(const int8_t *q, float *e);

float ss_dot(const float *a, const float *b);

/* Profile from n >= 2 unit-norm embeddings: writes the prototype, each example's leave-one-out
 * similarity (loo[n], may be NULL) and returns the threshold. */
float ss_make_profile(const float (*emb)[SS_EMB], int n, float *proto, float *loo);

#ifdef __cplusplus
}
#endif
