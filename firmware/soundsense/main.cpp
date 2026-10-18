/* Sound Sense: a home sound monitor for deaf and hard-of-hearing people (docs/SOUND-SENSE.md).
 *
 * Listening: the PDM mics at 16 kHz (../app/kws.c) give a 40-band log-mel frame every 20 ms.
 * Every 100 ms the last 1 s goes through the Sound Sense encoder on the Ethos-U85 (64-d embedding)
 * for the live view. Each sound onset (ss_audio.c) is classified once, 0.8 s later, when the onset
 * sits 0.2 s into the window, as in training and teaching: cosine against the built-in and taught
 * profiles; the best one counts if it clears its threshold. Knocks are paired with the BMI323
 * (ss_imu.c) to tell a knock felt through the table from one only heard (TV).
 *
 * Joystick, as read facing the screen: UP teach a sound, LEFT timeline, RIGHT stats, press = OK.
 * Host control over J-Link: ss_cmd / ss_arg, see host_command(). Logs go to RTT ([SS], [TEACH],
 * [C1] lines).
 */
#include "RTE_Components.h"
#include CMSIS_device_header

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "executorch_runner.h"
#include "kws.h"
#include "ss_audio.h"
#include "ss_board.h"
#include "ss_gfx.h"
#include "ss_imu.h"
#include "ss_match.h"
#include "ui.h"
#include "ui_fonts.h"
#include "ui_led.h"

#include "assets/ss_builtin_profiles.h"
#include "assets/ss_encoder_io.h"
#include "assets/ss_model_data.h"
#include "assets/ss_testvec.h"

#define IN_LEN         (KWS_FRAMES * KWS_NMEL)
#define LIVE_EVERY     5                      /* frames (20 ms) between live-view inferences */
#define DECIDE_DELAY   (KWS_SR * 8 / 10)      /* onset to decision: the onset lands 0.2 s into the window */
#define HOLDOFF        (KWS_SR * 12 / 10)     /* onsets closer than this to the previous one are the same sound */
#define MIN_SNR_DB     8.0f                   /* loudest 20 ms over the noise floor, to classify at all */
#define TEACH_SNR_DB   10.0f
#define MAYBE_MARGIN   0.05f                  /* under thr + this: "Maybe: x?" */
#define FELT_WIN_MS    100                    /* IMU knock within this of the audio onset = felt */
#define KEY_QUIET_MS   800                    /* ignore sounds right after a joystick press (its click and shake) */
#define MERGE_MS       30000                  /* same sound again within this: one entry, count + 1 */
#define BANNER_MS      6000
#define ALERT_MS       15000
#define MAX_EVENTS     32
#define MAX_TAUGHT     8
#define SHOTS          5
#define MAX_PROF       (SS_NUM_BUILTIN + MAX_TAUGHT)

extern "C" {
volatile uint32_t ss_cmd, ss_arg;   /* host commands, written over J-Link */
}

/* ---------- profiles ---------- */

struct look { const char *key, *label; ui_icon icon; ui_category cat; ui_urgency urg; };

static const look kBuiltin[] = {
    {"knock", "Knock", UI_ICON_KNOCK, UI_CAT_DOOR, UI_IMPORTANT},
    {"dog", "Dog barking", UI_ICON_DOG, UI_CAT_ANIMAL, UI_INFO},
    {"baby", "Baby crying", UI_ICON_BABY, UI_CAT_FAMILY, UI_IMPORTANT},
    {"alarm", "Alarm", UI_ICON_ALARM, UI_CAT_SAFETY, UI_IMPORTANT},
    {"glass", "Glass breaking", UI_ICON_GLASS, UI_CAT_SAFETY, UI_URGENT},
    {"siren", "Siren", UI_ICON_SIREN, UI_CAT_SAFETY, UI_URGENT},
    {"water", "Water running", UI_ICON_WATER, UI_CAT_WATER, UI_INFO},
    {"speech", "Speech", UI_ICON_SPEECH, UI_CAT_VOICE, UI_INFO},
    {"background", "Quiet", UI_ICON_UNKNOWN, UI_CAT_OTHER, UI_INFO},
};

/* Names offered when teaching. */
static const look kPresets[] = {
    {"keys", "Keys", UI_ICON_CUSTOM, UI_CAT_DOOR, UI_IMPORTANT},
    {"doorbell", "Doorbell", UI_ICON_DOORBELL, UI_CAT_DOOR, UI_IMPORTANT},
    {"clap", "Clap", UI_ICON_CUSTOM, UI_CAT_OTHER, UI_IMPORTANT},
    {"microwave", "Microwave", UI_ICON_MICROWAVE, UI_CAT_APPLIANCE, UI_INFO},
    {"kettle", "Kettle", UI_ICON_KETTLE, UI_CAT_APPLIANCE, UI_INFO},
    {"phone", "Phone", UI_ICON_PHONE, UI_CAT_APPLIANCE, UI_IMPORTANT},
    {"washer", "Washer", UI_ICON_WASHER, UI_CAT_APPLIANCE, UI_INFO},
    {"name", "My name", UI_ICON_NAME, UI_CAT_FAMILY, UI_IMPORTANT},
};
#define N_PRESETS ((int)(sizeof(kPresets) / sizeof(kPresets[0])))

struct profile {
    const look *lk;
    const float *proto;
    float thr;
    bool alerting;
    int preset;   /* taught: index into kPresets; built-in: -1 */
};

static profile prof[MAX_PROF];
static int n_prof, n_taught;
static float taught_proto[MAX_TAUGHT][SS_EMB];
static int knock_prof = -1;

static const look kFallback = {"?", "Sound", UI_ICON_UNKNOWN, UI_CAT_OTHER, UI_INFO};

/* Built-in profiles in RAM, so they can be re-measured on the board (G3 row 7): the trained ones,
 * replaced by assets/ss_board_profiles.h where that has a board measurement. */
static float builtin_proto[SS_NUM_BUILTIN][SS_EMB];
static float builtin_thr[SS_NUM_BUILTIN];
#if __has_include("assets/ss_board_profiles.h")
#include "assets/ss_board_profiles.h"
#else
#define SS_NUM_BOARD 0
static const char *const ss_board_names[1] = {""};
static const float ss_board_thr[1] = {0};
static const float ss_board_proto[1][SS_EMB] = {};
#endif

static void profiles_init(void)
{
    n_prof = 0;
    for (int i = 0; i < SS_NUM_BUILTIN; i++) {
        const look *lk = &kFallback;
        for (const look &l : kBuiltin)
            if (!strcmp(l.key, ss_builtin_names[i])) lk = &l;
        memcpy(builtin_proto[i], ss_builtin_proto[i], sizeof(builtin_proto[i]));
        builtin_thr[i] = ss_builtin_thr[i];
        for (int k = 0; k < SS_NUM_BOARD; k++)
            if (!strcmp(ss_board_names[k], ss_builtin_names[i])) {
                memcpy(builtin_proto[i], ss_board_proto[k], sizeof(builtin_proto[i]));
                builtin_thr[i] = ss_board_thr[k];
                printf("[SS] '%s': board-measured profile, thr %.3f\r\n", ss_builtin_names[i], (double)builtin_thr[i]);
            }
        prof[n_prof] = {lk, builtin_proto[i], builtin_thr[i], ss_builtin_alerting[i] != 0, -1};
        if (!strcmp(ss_builtin_names[i], "knock")) knock_prof = n_prof;
        n_prof++;
    }
}

/* Taught profile for a preset: replaces an earlier one of the same name. */
static void profile_add(int preset, const float *proto, float thr)
{
    int slot = -1;
    for (int k = 0; k < n_taught; k++)
        if (prof[SS_NUM_BUILTIN + k].preset == preset) slot = k;
    if (slot < 0) {
        if (n_taught >= MAX_TAUGHT) slot = MAX_TAUGHT - 1;
        else slot = n_taught++;
    }
    memcpy(taught_proto[slot], proto, sizeof(taught_proto[slot]));
    prof[SS_NUM_BUILTIN + slot] = {&kPresets[preset], taught_proto[slot], thr, true, preset};
    n_prof = SS_NUM_BUILTIN + n_taught;
}

/* ---------- encoder ---------- */

static uint32_t npu_us, runner_us, feat_us, n_infer;

static bool infer(const int8_t *in, int8_t *out)
{
    const uint32_t t0 = ss_cycles();
    if (executorch_run_inference(in, IN_LEN, out, SS_EMB) != 0) return false;
    runner_us = ss_cyc_to_us(ss_cycles() - t0);
    npu_us = ss_cyc_to_us(executorch_last_execute_cycles());
    n_infer++;
    return true;
}

static void score_all(const float *e, float *s)
{
    for (int p = 0; p < n_prof; p++) s[p] = ss_dot(e, prof[p].proto);
}

static int best_of(const float *s, int *second)
{
    int b = 0, b2 = -1;
    for (int p = 1; p < n_prof; p++) {
        if (s[p] > s[b]) { b2 = b; b = p; }
        else if (b2 < 0 || s[p] > s[b2]) b2 = p;
    }
    if (second) *second = b2;
    return b;
}

/* Boot self-test (C1): C features vs Python on a known clip, the NPU output vs the host, timing. */
static void self_test(void)
{
    static int8_t in[IN_LEN];
    int8_t out[SS_EMB];
    uint32_t t0 = ss_cycles();
    kws_features_of_clip(ss_test_clip, in, SS_IN_SCALE, SS_IN_ZP);
    const uint32_t f_us = ss_cyc_to_us(ss_cycles() - t0);
    int max_in = 0, off_in = 0;
    for (int i = 0; i < IN_LEN; i++) {
        const int d = abs(in[i] - ss_test_input[i]);
        if (d > max_in) max_in = d;
        off_in += d > 0;
    }
    /* the encoder on the host's exact input, then on ours; 10 runs for the timing */
    uint32_t npu_sum = 0, run_sum = 0;
    int max_out = 0;
    for (int r = 0; r < 10; r++) {
        if (!infer(r == 0 ? ss_test_input : in, out)) ss_halt("self-test inference");
        npu_sum += npu_us;
        run_sum += runner_us;
        if (r == 0)
            for (int i = 0; i < SS_EMB; i++) {
                const int d = abs(out[i] - ss_test_output[i]);
                if (d > max_out) max_out = d;
            }
    }
    float e[SS_EMB], ref[SS_EMB], s[MAX_PROF];
    ss_embed(out, e);
    ss_embed(ss_test_output, ref);
    score_all(e, s);
    const int b = best_of(s, NULL);
    printf("[C1] self-test: features %lu us, %d/%d inputs differ from Python (max %d LSB); output on the "
           "host input max diff %d LSB; ours vs host cos %.4f; best '%s' %.2f (expected '%s'); "
           "npu %lu us, runner %lu us (mean of 10); model %u bytes\r\n",
           (unsigned long)f_us, off_in, IN_LEN, max_in, max_out, (double)ss_dot(e, ref), ss_builtin_names[b],
           (double)s[b], SS_TEST_NAME, (unsigned long)(npu_sum / 10), (unsigned long)(run_sum / 10),
           ss_encoder_pte_len);
}

/* ---------- events ---------- */

static ui_event events[MAX_EVENTS];   /* newest first */
static int n_events;

static ui_event *add_event(const look *lk, bool maybe, ui_felt felt, float score, uint32_t t, bool *merged)
{
    ui_event *e = &events[0];
    if (n_events && e->label == lk->label && t - (e->t_ms + e->dur_ms) < MERGE_MS) {
        e->count++;
        e->dur_ms = t - e->t_ms;
        e->maybe = e->maybe && maybe;
        if (felt == UI_FELT_YES || e->felt == UI_FELT_NA) e->felt = felt;
        e->acked = false;
        *merged = true;
        return e;
    }
    if (n_events < MAX_EVENTS) n_events++;
    memmove(&events[1], &events[0], (size_t)(n_events - 1) * sizeof(ui_event));
    memset(e, 0, sizeof(*e));
    e->label = lk->label;
    e->icon = lk->icon;
    e->cat = lk->cat;
    e->urgency = lk->urg;
    e->maybe = maybe;
    e->felt = felt;
    e->confidence = (uint8_t)(score < 0 ? 0 : score > 1 ? 100 : score * 100);
    e->count = 1;
    e->t_ms = t;
    *merged = false;
    return e;
}

/* ---------- IMU knocks ---------- */

static bool imu_ok;
static uint32_t knock_ms[8];
static float knock_mg[8];
static uint32_t n_knocks;

static uint32_t ev_ms(uint32_t cyc) { return ss_ms() - (ss_cycles() - cyc) / (SystemCoreClock / 1000u); }

/* Was there a knock through the table within FELT_WIN_MS of t? */
static ui_felt felt_at(uint32_t t, float *mg)
{
    if (!imu_ok) return UI_FELT_NA;
    for (uint32_t i = 0; i < 8 && i < n_knocks; i++) {
        const uint32_t k = (n_knocks - 1 - i) % 8;
        const int32_t dt = (int32_t)(t - knock_ms[k]);
        if (dt <= FELT_WIN_MS && dt >= -FELT_WIN_MS) {
            *mg = knock_mg[k];
            return UI_FELT_YES;
        }
    }
    return UI_FELT_HEARD_ONLY;
}

/* ---------- screens ---------- */

enum screen { HOME, ALERT, TIMELINE, STATS, TEACH };
static screen scr = HOME;
static bool redraw = true;

static ui_home home;
static ui_guess top3[3];
static ui_event alert_ev, banner_ev;
static uint32_t alert_t0, banner_t0;
static bool banner_on;
static int alert_phase, tl_first;

enum tphase { T_NAMING, T_REC, T_TEST };
static struct {
    tphase ph;
    int preset, n;
    float emb[SHOTS][SS_EMB], proto[SS_EMB], loo[SHOTS], thr;
    ui_teach ui;
} tc;

static void teach_open(int preset, bool start)
{
    memset(&tc, 0, sizeof(tc));
    tc.preset = preset;
    tc.ph = start ? T_REC : T_NAMING;
    tc.ui.n_shots = SHOTS;
    tc.ui.test_score = -1;
    scr = TEACH;
    redraw = true;
    if (start) printf("[TEACH] '%s': make the sound %d times\r\n", kPresets[preset].label, SHOTS);
}

static void teach_save(void)
{
    if (tc.ph != T_TEST) return;
    profile_add(tc.preset, tc.proto, tc.thr);
    printf("[TEACH] saved '%s' thr %.3f; %d taught\r\n", kPresets[tc.preset].label, (double)tc.thr, n_taught);
    scr = HOME;
    redraw = true;
}

static void go_home(void)
{
    if (scr == ALERT) {
        for (int i = 0; i < n_events; i++)
            if (events[i].label == alert_ev.label) events[i].acked = true;
    }
    scr = HOME;
    redraw = true;
}

static void present_dirty(void)
{
    int y0, y1;
    if (gfx_dirty_rows(&y0, &y1)) {
        ss_present_rows(y0, y1);
        gfx_dirty_reset();
    }
}

/* ---------- stats ---------- */

static uint32_t lat[16], n_lat;
static uint32_t work_cyc, win_infer0;
static float cpu_pct, infer_rate;

static void stats_fill(ui_stats *s)
{
    memset(s, 0, sizeof(*s));
    s->npu_us = npu_us;
    s->runner_us = runner_us;
    s->feat_us = feat_us;
    uint32_t v[16];
    const int n = n_lat < 16 ? (int)n_lat : 16;
    memcpy(v, lat, sizeof(v));
    for (int i = 1; i < n; i++)   /* sort */
        for (int j = i; j > 0 && v[j] < v[j - 1]; j--) { uint32_t t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
    if (n) {
        s->latency_med_ms = v[n / 2];
        s->latency_p90_ms = v[(n * 9) / 10 < n ? (n * 9) / 10 : n - 1];
    }
    s->cpu_active_pct = cpu_pct;
    s->infer_per_s = infer_rate;
    s->npu_busy_pct = infer_rate * npu_us / 10000.0f;
    s->model_kb = ss_encoder_pte_len / 1024;
    s->emb_dim = SS_EMB;
    s->n_builtin = SS_NUM_BUILTIN;
    s->n_taught = n_taught;
    s->uptime_s = ss_ms() / 1000;
    s->dropped = kws_audio_dropped();
}

/* ---------- decisions ---------- */

static int16_t clip_buf[KWS_SR];
static uint32_t last_key_ms = 0x80000000u;

/* Loudest 20 ms block of [first, first + 1 s) over the noise floor, and how many samples clipped. */
static float snr_of(uint32_t first, int *clipped)
{
    *clipped = 0;
    if (ss_audio_copy(first, clip_buf, KWS_SR) != 0) return 0.0f;
    float best = 0.0f;
    for (int b = 0; b + KWS_HOP <= KWS_SR; b += KWS_HOP) {
        float acc = 0.0f;
        for (int i = 0; i < KWS_HOP; i++) {
            const float x = clip_buf[b + i] / 32768.0f;
            acc += x * x;
            *clipped += clip_buf[b + i] >= 32700 || clip_buf[b + i] <= -32700;
        }
        if (acc > best) best = acc;
    }
    const float db = best > 0.0f ? 10.0f * log10f(best / KWS_HOP) : -120.0f;
    return db - ss_audio_floor_db();
}

static void teach_shot(const float *e, float snr, int clipped)
{
    if (tc.ph == T_TEST) {
        const float s = ss_dot(e, tc.proto);
        tc.ui.test_score = (int)(s * 100.0f + 0.5f);
        printf("[TEACH] test: match %.3f (thr %.3f) %s\r\n", (double)s, (double)tc.thr, s >= tc.thr ? "accepted" : "rejected");
        redraw = true;
        return;
    }
    if (tc.ph != T_REC || tc.n >= SHOTS) return;
    if (snr < TEACH_SNR_DB) {
        tc.ui.shot[tc.n] = UI_SHOT_BAD;
        tc.ui.shot_note[tc.n] = "too quiet";
        printf("[TEACH] shot %d: too quiet (+%.1f dB)\r\n", tc.n + 1, (double)snr);
        redraw = true;
        return;
    }
    memcpy(tc.emb[tc.n], e, sizeof(tc.emb[0]));
    tc.ui.shot[tc.n] = UI_SHOT_OK;
    tc.ui.shot_db[tc.n] = (int8_t)(snr > 99 ? 99 : snr);
    tc.ui.shot_note[tc.n] = clipped ? "clipped" : "ok";
    tc.n++;
    printf("[TEACH] shot %d/%d: +%.1f dB%s\r\n", tc.n, SHOTS, (double)snr, clipped ? ", clipped" : "");
    if (tc.n == SHOTS) {
        tc.thr = ss_make_profile(tc.emb, SHOTS, tc.proto, tc.loo);
        tc.ph = T_TEST;
        printf("[TEACH] profile: thr %.3f, leave-one-out %.2f %.2f %.2f %.2f %.2f; make it once more to test, "
               "press to save\r\n", (double)tc.thr, (double)tc.loo[0], (double)tc.loo[1], (double)tc.loo[2],
               (double)tc.loo[3], (double)tc.loo[4]);
    }
    redraw = true;
}

static void alert_raise(ui_event *e)
{
    if (e->urgency == UI_INFO) {
        banner_ev = *e;
        banner_on = true;
        banner_t0 = ss_ms();
        if (scr == HOME) redraw = true;
        return;
    }
    if (scr == TEACH) return;
    alert_ev = *e;
    alert_t0 = ss_ms();
    alert_phase = 0;
    scr = ALERT;
    redraw = true;
}

/* Classify the sound whose onset was at sample `onset`: called once the window holds it 0.2 s in. */
/* Re-measuring a built-in on the board: embeddings of the next sounds replace its profile. */
#define CAL_MAX 16
static int cal_idx = -1, cal_n;
static float cal_emb[CAL_MAX][SS_EMB];

static void cal_finish(void)
{
    if (cal_idx < 0) return;
    const int i = cal_idx;
    cal_idx = -1;
    if (cal_n < 3) {
        printf("[CAL] '%s': only %d examples, profile unchanged\r\n", ss_builtin_names[i], cal_n);
        return;
    }
    float proto[SS_EMB], loo[CAL_MAX];
    const float thr = ss_make_profile(cal_emb, cal_n, proto, loo);
    const float was = ss_dot(proto, ss_builtin_proto[i]);
    memcpy(builtin_proto[i], proto, sizeof(proto));
    builtin_thr[i] = thr;
    prof[i].thr = thr;
    printf("[CAL] '%s' from %d board examples: thr %.3f (trained %.3f), cos to the trained profile %.3f\r\n",
           ss_builtin_names[i], cal_n, (double)thr, (double)ss_builtin_thr[i], (double)was);
    printf("[CAL] proto %s %.4f", ss_builtin_names[i], (double)thr);
    for (int k = 0; k < SS_EMB; k++) printf(" %.5f", (double)proto[k]);
    printf("\r\n");
}

static void decide(uint32_t onset, uint32_t onset_ms)
{
    static int8_t in[IN_LEN];
    int8_t out[SS_EMB];
    float e[SS_EMB], s[MAX_PROF];
    const uint32_t t0 = ss_cycles();
    kws_make_input(in, SS_IN_SCALE, SS_IN_ZP);
    if (!infer(in, out)) ss_halt("inference");
    work_cyc += ss_cycles() - t0;
    ss_embed(out, e);
    score_all(e, s);
    int clipped;
    const float snr = snr_of(onset - KWS_SR / 5, &clipped);
    const uint32_t now = ss_ms();

    if (scr == TEACH) {
        if (tc.ph != T_NAMING) teach_shot(e, snr, clipped);
        return;
    }
    if (cal_idx >= 0) {
        if (snr >= MIN_SNR_DB && cal_n < CAL_MAX) {
            memcpy(cal_emb[cal_n++], e, sizeof(e));
            printf("[CAL] '%s' example %d: +%.1f dB, cos to the trained profile %.2f\r\n", ss_builtin_names[cal_idx],
                   cal_n, (double)snr, (double)ss_dot(e, ss_builtin_proto[cal_idx]));
        }
        return;
    }
    if ((int32_t)(onset_ms - last_key_ms) > -300 && (int32_t)(onset_ms - last_key_ms) < KEY_QUIET_MS) {
        printf("[SS] t=%lu skipped (joystick)\r\n", (unsigned long)onset_ms);
        return;
    }
    int b2;
    const int b = best_of(s, &b2);
    float mg = 0.0f;
    const ui_felt felt = felt_at(onset_ms, &mg);
    const bool over = s[b] >= prof[b].thr;
    const bool loud = snr >= MIN_SNR_DB;

    const look *lk = NULL;
    bool maybe = false;
    ui_felt f = UI_FELT_NA;
    float score = s[b];
    if (loud && over && prof[b].alerting && b != knock_prof) {
        lk = prof[b].lk;
        maybe = s[b] < prof[b].thr + MAYBE_MARGIN;
    } else if (loud && ((b == knock_prof && over) || felt == UI_FELT_YES)) {
        /* a knock: the encoder says so, or the table felt it (then "maybe" unless both agree) */
        lk = prof[knock_prof].lk;
        f = felt;
        score = s[knock_prof];
        maybe = !(b == knock_prof && over);
    }
    printf("[SS] t=%lu +%.1f dB%s best '%s' %.2f (thr %.2f), 2nd '%s' %.2f, felt %s (%.1f mg) -> %s%s\r\n",
           (unsigned long)onset_ms, (double)snr, clipped ? " clipped" : "", prof[b].lk->label, (double)s[b],
           (double)prof[b].thr, b2 >= 0 ? prof[b2].lk->label : "-", b2 >= 0 ? (double)s[b2] : 0.0,
           felt == UI_FELT_YES ? "yes" : felt == UI_FELT_HEARD_ONLY ? "no" : "n/a", (double)mg,
           lk ? (maybe ? "maybe " : "") : "", lk ? lk->label : (loud ? "unknown" : "quiet"));
    if (!lk) return;

    bool merged;
    ui_event *ev = add_event(lk, maybe, f, score, onset_ms, &merged);
    /* a knock only heard (TV?) is shown as a banner, not a full-screen alert */
    if (lk == prof[knock_prof].lk && f == UI_FELT_HEARD_ONLY) ev->urgency = UI_INFO;
    lat[n_lat++ % 16] = now - onset_ms;
    alert_raise(ev);
    if (scr == HOME) redraw = true;
}

/* ---------- host commands and joystick ---------- */

static uint32_t host_keys;

/* 1 teach preset ss_arg (straight to recording), 2 save the taught sound, 3 home / cancel,
 * 4 forget taught sounds, 5 stats, 6 timeline, 7 clock (ss_arg = ms after midnight now),
 * 8 press joystick keys (ss_arg = SS_JOY_* bits), 9 re-measure built-in ss_arg from the next sounds,
 * 10 finish re-measuring. */
static void host_command(void)
{
    const uint32_t c = ss_cmd, a = ss_arg;
    if (!c) return;
    ss_cmd = 0;
    switch (c) {
    case 1: teach_open((int)(a % N_PRESETS), true); break;
    case 2: teach_save(); break;
    case 3: go_home(); break;
    case 4: n_taught = 0; n_prof = SS_NUM_BUILTIN; printf("[SS] taught sounds cleared\r\n"); break;
    case 5: scr = STATS; redraw = true; break;
    case 6: scr = TIMELINE; tl_first = 0; redraw = true; break;
    case 7: ui_set_clock(a - ss_ms()); redraw = true; break;
    case 8: host_keys |= a; break;
    case 9: cal_finish(); cal_idx = (int)(a % SS_NUM_BUILTIN); cal_n = 0;
            printf("[CAL] '%s': measuring\r\n", ss_builtin_names[cal_idx]); break;
    case 10: cal_finish(); break;
    }
}

static void on_keys(uint32_t k)
{
    switch (scr) {
    case HOME:
        if (k & SS_JOY_UP) teach_open(0, false);
        else if (k & SS_JOY_LEFT) { scr = TIMELINE; tl_first = 0; redraw = true; }
        else if (k & SS_JOY_RIGHT) { scr = STATS; redraw = true; }
        else if (k & SS_JOY_CENTER) { banner_on = false; redraw = true; }
        break;
    case ALERT:
        go_home();
        break;
    case TIMELINE:
        if (k & SS_JOY_UP) { if (tl_first > 0) tl_first--; redraw = true; }
        else if (k & SS_JOY_DOWN) { if (tl_first + 1 < n_events) tl_first++; redraw = true; }
        else go_home();
        break;
    case STATS:
        go_home();
        break;
    case TEACH:
        if (tc.ph == T_NAMING) {
            if (k & SS_JOY_UP) tc.preset = (tc.preset + N_PRESETS - 1) % N_PRESETS;
            else if (k & SS_JOY_DOWN) tc.preset = (tc.preset + 1) % N_PRESETS;
            else if (k & SS_JOY_CENTER) teach_open(tc.preset, true);
            else if (k & SS_JOY_RIGHT) go_home();
            redraw = true;
        } else {
            if (k & SS_JOY_LEFT) teach_open(tc.preset, true);              /* retry */
            else if (k & SS_JOY_RIGHT) { printf("[TEACH] cancelled\r\n"); go_home(); }
            else if (k & SS_JOY_CENTER) teach_save();
        }
        break;
    }
}

/* ---------- drawing ---------- */

static void draw(uint32_t now, bool live_only)
{
    const uint32_t t0 = ss_cycles();
    switch (scr) {
    case HOME:
        home.listening = true;
        home.mode = "Home";
        home.room_db = ss_audio_block_db();
        home.floor_db = ss_audio_floor_db();
        for (int i = 0; i < 3; i++) home.top[i] = top3[i];
        home.recent = events;
        home.n_recent = n_events;
        if (live_only) {
            ui_home_update_live(&home);
        } else {
            ui_home_draw(&home, now);
            if (banner_on) ui_banner_draw(&banner_ev, now);
        }
        break;
    case ALERT:
        ui_alert_draw(&alert_ev, now, alert_phase);
        break;
    case TIMELINE:
        ui_timeline_draw(events, n_events, now, tl_first);
        break;
    case STATS: {
        ui_stats st;
        stats_fill(&st);
        ui_stats_draw(&st);
        break;
    }
    case TEACH: {
        const look &l = kPresets[tc.preset];
        tc.ui.label = l.label;
        tc.ui.icon = l.icon;
        tc.ui.cat = l.cat;
        tc.ui.level_db = ss_audio_block_db();
        tc.ui.floor_db = ss_audio_floor_db();
        ui_teach_draw(&tc.ui);
        if (tc.ph == T_NAMING) {
            const gfx_color bg = {12, 15, 21}, fg = {240, 228, 66};
            gfx_fill(0, 300, GFX_W, 60, bg);
            gfx_text_center(&ui_font_bold, GFX_W / 2, 314, "UP / DOWN: pick a name", fg, 26);
            gfx_fill(0, 600, GFX_W, 48, bg);
            gfx_text_center(&ui_font_book, GFX_W / 2, 610, "Press to start, RIGHT to cancel", fg, 24);
        }
        break;
    }
    }
    present_dirty();
    work_cyc += ss_cycles() - t0;
}

static void set_leds(uint32_t now)
{
    ui_led led[2] = {};
    if (scr == ALERT) ui_led_pattern(alert_ev.urgency, alert_ev.cat, alert_ev.maybe, now - alert_t0, led);
    else if (banner_on) ui_led_pattern(UI_INFO, banner_ev.cat, banner_ev.maybe, now - banner_t0, led);
    else if (scr == TEACH && tc.ph != T_NAMING) led[0].g = (now / 500) & 1;   /* slow green: recording */
    static uint32_t shown[2] = {~0u, ~0u};
    for (int i = 0; i < 2; i++) {
        const uint32_t c = (led[i].r ? SS_LED_RED : 0) | (led[i].g ? SS_LED_GREEN : 0) | (led[i].b ? SS_LED_BLUE : 0);
        if (c != shown[i]) ss_led_set(i, shown[i] = c);
    }
}

/* ---------- main ---------- */

int main(void)
{
    ss_board_init();
    ss_gfx_init();
    ss_joy_init();
    if (ss_led_init() != 0) printf("[SS] LED init failed\r\n");
    printf("\r\n[SS] Sound Sense starting\r\n");

    if (executorch_init(ss_encoder_pte, ss_encoder_pte_len) != 0) ss_halt("executorch_init");
    executorch_set_verbose(0);
    profiles_init();
    self_test();

    const int ir = ss_imu_init();
    imu_ok = ir == 0;
    if (!imu_ok) printf("[SS] IMU init failed (%d): knocks can't be felt\r\n", ir);

    ui_init(ss_gfx_back());
    ss_audio_init();
    if (kws_audio_start() != 0) ss_halt("microphone start");
    printf("[SS] listening; %d built-in sounds\r\n", SS_NUM_BUILTIN);

    static int8_t in[IN_LEN];
    int8_t out[SS_EMB];
    float e[SS_EMB], s[MAX_PROF];
    int frames = 0;
    bool pend = false;
    uint32_t pend_sample = 0, pend_ms = 0, last_onset = 0;
    bool any_onset = false;
    uint32_t last_imu = 0, last_live_draw = 0, last_slow = 0, last_log = 0, last_key = 0;
    uint32_t win_ms0 = ss_ms();

    for (;;) {
        frames += kws_audio_poll();
        const uint32_t now = ss_ms();
        host_command();

        if (imu_ok && now - last_imu >= 5) {
            last_imu = now;
            ss_imu_poll();
            ss_knock k;
            while (ss_imu_knock(&k)) {
                knock_ms[n_knocks % 8] = ev_ms(k.cyc);
                knock_mg[n_knocks % 8] = k.peak_mg;
                n_knocks++;
            }
        }

        /* joystick, with a 250 ms lock-out against switch bounce */
        uint32_t keys = ss_joy_pressed() | host_keys;
        host_keys = 0;
        if (keys && now - last_key >= 250) {
            last_key = now;
            last_key_ms = now;
            on_keys(keys);
        }

        /* onsets: one decision per sound, 0.8 s after it starts */
        ss_onset o;
        while (ss_audio_onset(&o)) {
            if (pend || (any_onset && o.sample - last_onset < HOLDOFF)) continue;
            pend = true;
            any_onset = true;
            last_onset = pend_sample = o.sample;
            pend_ms = ev_ms(o.cyc);
        }
        if (pend && ss_audio_samples() >= pend_sample + DECIDE_DELAY) {
            pend = false;
            decide(pend_sample, pend_ms);
        }

        /* live view: encoder every 100 ms, spectrogram columns, best three matches */
        if (frames >= LIVE_EVERY) {
            const int n_new = frames > KWS_FRAMES ? KWS_FRAMES : frames;
            frames = 0;
            const uint32_t t0 = ss_cycles();
            kws_make_input(in, SS_IN_SCALE, SS_IN_ZP);
            feat_us = ss_cyc_to_us(ss_cycles() - t0);
            if (!infer(in, out)) ss_halt("inference");
            work_cyc += ss_cycles() - t0;
            ss_embed(out, e);
            score_all(e, s);
            for (int f = KWS_FRAMES - n_new; f < KWS_FRAMES; f++) {
                uint8_t col[KWS_NMEL];
                for (int b = 0; b < KWS_NMEL; b++) {
                    const int v = (in[f * KWS_NMEL + b] + 40) * 3;   /* quiet room about -32: dark */
                    col[b] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
                }
                ui_spectro_push(col);
            }
            /* best three matches, "Quiet" and "Speech" included */
            bool used[MAX_PROF] = {};
            for (int i = 0; i < 3; i++) {
                int b = -1;
                for (int p = 0; p < n_prof; p++)
                    if (!used[p] && (b < 0 || s[p] > s[b])) b = p;
                if (b < 0) { top3[i] = {"", 0, false}; continue; }
                used[b] = true;
                top3[i].label = prof[b].lk->label;
                top3[i].score = (uint8_t)(s[b] < 0 ? 0 : s[b] * 100);
                top3[i].over_threshold = s[b] >= prof[b].thr;
            }
            if (scr == HOME && !redraw && now - last_live_draw >= 100) {
                last_live_draw = now;
                draw(now, true);
            }
        }

        /* timeouts, the urgent strobe (one flash a second) and slow refreshes */
        if (banner_on && now - banner_t0 > BANNER_MS) {
            banner_on = false;
            if (scr == HOME) redraw = true;
        }
        if (scr == ALERT) {
            if (now - alert_t0 > ALERT_MS) {
                scr = HOME;
                redraw = true;
            } else if (alert_ev.urgency == UI_URGENT && (int)((now - alert_t0) / 500 % 2) != alert_phase) {
                alert_phase ^= 1;
                redraw = true;
            }
        }
        if (now - last_slow >= 250) {
            last_slow = now;
            if (scr == TEACH || (scr == STATS && now % 1000 < 250)) redraw = true;
        }
        if (redraw) {
            redraw = false;
            draw(now, false);
        }
        set_leds(now);

        if (now - last_log >= 10000) {
            last_log = now;
            cpu_pct = 100.0f * work_cyc / ((float)(now - win_ms0) * (SystemCoreClock / 1000u));
            infer_rate = (n_infer - win_infer0) * 1000.0f / (float)(now - win_ms0);
            work_cyc = 0;
            win_infer0 = n_infer;
            win_ms0 = now;
            const ss_imu_stats *is = ss_imu_get_stats();
            printf("[SS] stats t=%lu level %d dB floor %d dB, npu %lu us runner %lu us feat %lu us, %.1f inf/s, "
                   "busy %.0f%%, dropped %lu, imu %lu samples %lu errors, knocks %lu, events %d\r\n",
                   (unsigned long)now, (int)ss_audio_block_db(), (int)ss_audio_floor_db(), (unsigned long)npu_us,
                   (unsigned long)runner_us, (unsigned long)feat_us, (double)infer_rate, (double)cpu_pct,
                   (unsigned long)kws_audio_dropped(), (unsigned long)(imu_ok ? is->samples : 0),
                   (unsigned long)(imu_ok ? is->errors : 0), (unsigned long)n_knocks, n_events);
        }
    }
}
