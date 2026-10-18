/* Sound Sense matching (see ss_match.h). */
#include "ss_match.h"

#include <math.h>
#include <string.h>

static void normalize(float *e)
{
    float s = 0.0f;
    for (int i = 0; i < SS_EMB; i++) s += e[i] * e[i];
    const float k = s > 0.0f ? 1.0f / sqrtf(s) : 0.0f;
    for (int i = 0; i < SS_EMB; i++) e[i] *= k;
}

void ss_embed(const int8_t *q, float *e)
{
    for (int i = 0; i < SS_EMB; i++) e[i] = (q[i] - SS_OUT_ZP) * SS_OUT_SCALE;
    normalize(e);
}

float ss_dot(const float *a, const float *b)
{
    float s = 0.0f;
    for (int i = 0; i < SS_EMB; i++) s += a[i] * b[i];
    return s;
}

float ss_make_profile(const float (*emb)[SS_EMB], int n, float *proto, float *loo)
{
    float sum[SS_EMB] = {0};
    for (int k = 0; k < n; k++)
        for (int i = 0; i < SS_EMB; i++) sum[i] += emb[k][i];
    memcpy(proto, sum, sizeof(sum));
    normalize(proto);

    float lo = 1.0f;
    for (int k = 0; k < n; k++) {
        float rest[SS_EMB];
        for (int i = 0; i < SS_EMB; i++) rest[i] = sum[i] - emb[k][i];
        normalize(rest);
        const float s = ss_dot(emb[k], rest);
        if (loo) loo[k] = s;
        if (s < lo) lo = s;
    }
    float thr = lo - SS_THR_MARGIN;
    if (thr < SS_THR_MIN) thr = SS_THR_MIN;
    if (thr > SS_THR_MAX) thr = SS_THR_MAX;
    return thr;
}
