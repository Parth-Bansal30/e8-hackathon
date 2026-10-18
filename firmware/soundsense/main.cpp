/* Sound Sense: a home sound monitor for deaf and hard-of-hearing people (docs/SOUND-SENSE.md).
 *
 * Listening: the PDM mics at 16 kHz (../app/kws.c) give a 40-band log-mel frame every 20 ms.
 * Every 100 ms the last 1 s goes through the Sound Sense encoder on the Ethos-U85 (64-d embedding)
 * for the live view. Each sound onset (ss_audio.c) is classified once, 0.8 s later, when the onset
 * sits 0.2 s into the window, as in training and teaching: cosine against the built-in and taught
 * profiles; the best one counts if it clears its threshold. Knocks are paired with the BMI323
 * (ss_imu.c) to tell a knock felt through the table from one only heard (TV).
 *
 * Taught sounds are saved in the OSPI flash (ss_store.c) after each teach and loaded at boot.
 *
 * Boot self-test (S1) on the "Ready" screen: microphones, NPU against the stored reference, saved
 * sounds, vibration sensor, LEDs. Power (C5): the main loop sleeps (WFI) when no audio block is
 * waiting, and the live view's encoder runs only while the room is above its noise floor + 6 dB
 * (and 300 ms after); sound decisions always run.
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
#include "ss_store.h"
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
#define ALERT_MS       15000                  /* important alerts close after this; urgent ones wait for a press */
#define ESCALATE_MS    30000                  /* urgent alert with no press: strobe twice as fast, bigger text (A5) */
#define MAX_EVENTS     32
#define MAX_TAUGHT     8
#define SHOTS          5
#define MAX_PROF       (SS_NUM_BUILTIN + MAX_TAUGHT)
#define GATE_DB        6.0f                   /* live encoder only above the noise floor + this */
#define GATE_HOLD_MS   300                    /* ... and for this long after */
#define READY_MS       2500                   /* "Ready" screen, then home (stays on a failure) */
#define NIGHT_DARK_MS  15000                  /* night mode: home screen goes dark after this without a press (A6) */

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
static int8_t taught_ex[MAX_TAUGHT][SS_STORE_MAX_EX][SS_EMB];   /* the examples, for the flash copy */
static uint8_t taught_n_ex[MAX_TAUGHT];
static_assert(MAX_TAUGHT <= SS_STORE_MAX_PROF && SHOTS <= SS_STORE_MAX_EX, "flash record too small");
static int knock_prof = -1;

/* T4: sounds the user marked "wrong" for a profile. A sound closer to one of them than to the profile
 * itself doesn't count as that profile. Kept in flash for taught sounds; for built-ins until restart. */
#define CX_MIN 0.70f
static int8_t cx[MAX_PROF][SS_STORE_MAX_EX][SS_EMB];
static uint8_t n_cx[MAX_PROF];

static float cx_score(int p, const float *e)
{
    float best = -1.0f, c[SS_EMB];
    for (int k = 0; k < n_cx[p]; k++) {
        ss_store_f32(cx[p][k], c);
        const float d = ss_dot(e, c);
        if (d > best) best = d;
    }
    return best;
}

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

SS_COLD static void profiles_init(void)
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

/* Taught profile for a preset: replaces an earlier one of the same name. ex: n int8 examples. */
SS_COLD static int profile_add(int preset, const float *proto, float thr, const int8_t (*ex)[SS_EMB], int n)
{
    int slot = -1;
    for (int k = 0; k < n_taught; k++)
        if (prof[SS_NUM_BUILTIN + k].preset == preset) slot = k;
    if (slot < 0) {
        if (n_taught >= MAX_TAUGHT) slot = MAX_TAUGHT - 1;
        else slot = n_taught++;
    }
    memcpy(taught_proto[slot], proto, sizeof(taught_proto[slot]));
    if (n > SS_STORE_MAX_EX) n = SS_STORE_MAX_EX;
    memcpy(taught_ex[slot], ex, (size_t)n * SS_EMB);
    taught_n_ex[slot] = (uint8_t)n;
    prof[SS_NUM_BUILTIN + slot] = {&kPresets[preset], taught_proto[slot], thr, true, preset};
    n_cx[SS_NUM_BUILTIN + slot] = 0;
    n_prof = SS_NUM_BUILTIN + n_taught;
    return slot;
}

/* ---------- flash copy of the taught sounds ---------- */

static bool store_ok;
static uint32_t store_repeat;   /* host command 11: saves still to run back to back (C2 row 1) */
static ss_store_data store_img;

SS_COLD static void store_save(void)
{
    if (!store_ok) return;
    memset(&store_img, 0, sizeof(store_img));
    store_img.n_prof = n_taught;
    for (int k = 0; k < n_taught; k++) {
        const profile &p = prof[SS_NUM_BUILTIN + k];
        ss_store_prof &s = store_img.prof[k];
        strncpy(s.key, p.lk->key, SS_STORE_KEY - 1);
        s.icon = (uint8_t)p.lk->icon;
        s.urgency = (uint8_t)p.lk->urg;
        s.cat = (uint8_t)p.lk->cat;
        s.thr = p.thr;
        memcpy(s.proto, p.proto, sizeof(s.proto));
        s.n_ex = taught_n_ex[k];
        memcpy(s.ex, taught_ex[k], sizeof(s.ex));
        s.n_cx = n_cx[SS_NUM_BUILTIN + k];
        memcpy(s.cx, cx[SS_NUM_BUILTIN + k], sizeof(s.cx));
    }
    ss_store_save(&store_img);
}

/* C2 bench: every taught slot filled with a stand-in (a built-in's profile under a preset's name,
 * threshold 0.99 so it never alerts) for a full-size record. Command 4 forgets them. */
SS_COLD static void store_fill_test(void)
{
    int8_t ex[SHOTS][SS_EMB];
    for (int k = 0; k < MAX_TAUGHT; k++) {
        const float *p = builtin_proto[k % SS_NUM_BUILTIN];
        for (int j = 0; j < SHOTS; j++) ss_store_q8(p, ex[j]);
        profile_add(k % N_PRESETS, p, 0.99f, ex, SHOTS);
    }
    printf("[STORE] %d stand-in taught sounds for the C2 bench\r\n", n_taught);
    store_save();
}

/* 1 restored, 0 nothing saved yet, -1 the flash doesn't answer. */
SS_COLD static int store_load(void)
{
    const int ir = ss_store_init();
    store_ok = ir == 0;
    if (!store_ok) {
        printf("[STORE] flash init failed (%d): taught sounds won't be kept\r\n", ir);
        return -1;
    }
    if (ss_store_load(&store_img) != 0) {
        printf("[STORE] nothing saved yet (%lu us)\r\n", (unsigned long)ss_store_get_stats()->load_us);
        return 0;
    }
    for (int i = 0; i < store_img.n_prof; i++) {
        const ss_store_prof &s = store_img.prof[i];
        int preset = -1;
        for (int k = 0; k < N_PRESETS; k++)
            if (!strcmp(kPresets[k].key, s.key)) preset = k;
        if (preset < 0) {
            printf("[STORE] skipped unknown sound '%s'\r\n", s.key);
            continue;
        }
        const int slot = profile_add(preset, s.proto, s.thr, s.ex, s.n_ex);
        n_cx[SS_NUM_BUILTIN + slot] = s.n_cx;
        memcpy(cx[SS_NUM_BUILTIN + slot], s.cx, sizeof(s.cx));
    }
    const ss_store_stats *st = ss_store_get_stats();
    printf("[STORE] loaded %d taught sounds from copy %c, seq %lu, in %lu us\r\n", n_taught, 'A' + st->copy,
           (unsigned long)st->seq, (unsigned long)st->load_us);
    return 1;
}

/* One step of a running save, between other work (one sector erase blocks about 82 ms). */
static void store_step(void)
{
    const int r = ss_store_step();
    if (r == 0) return;
    const ss_store_stats *st = ss_store_get_stats();
    printf("[STORE] save %lu %s: copy %c seq %lu, %lu bytes, %lu ms (max %lu), longest step %lu us, dropped %lu\r\n",
           (unsigned long)st->saves, r > 0 ? "ok" : "FAILED", st->copy < 0 ? '-' : 'A' + st->copy, (unsigned long)st->seq,
           (unsigned long)st->last_len, (unsigned long)st->last_ms, (unsigned long)st->max_ms,
           (unsigned long)st->step_max_us, (unsigned long)kws_audio_dropped());
    if (store_repeat && --store_repeat) store_save();
}

/* ---------- encoder ---------- */

static uint32_t npu_us, runner_us, feat_us, n_infer;
static uint64_t npu_sum_us;   /* for NPU busy % (C5) */

static bool infer(const int8_t *in, int8_t *out)
{
    const uint32_t t0 = ss_cycles();
    if (executorch_run_inference(in, IN_LEN, out, SS_EMB) != 0) return false;
    runner_us = ss_cyc_to_us(ss_cycles() - t0);
    npu_us = ss_cyc_to_us(executorch_last_execute_cycles());
    npu_sum_us += npu_us;
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

/* Boot self-test (C1): C features vs Python on a known clip, the NPU output vs the host, timing.
 * True if it matches: features within 1 LSB, output within 2 LSB, and the expected best match. */
SS_COLD static bool self_test(char *detail, size_t len)
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
    const bool ok = max_in <= 1 && max_out <= 2 && !strcmp(ss_builtin_names[b], SS_TEST_NAME);
    const uint32_t tenths = (npu_sum + 500) / 1000;   /* mean of 10 runs, in 0.1 ms */
    if (ok) snprintf(detail, len, "matches the reference, %lu.%lu ms", (unsigned long)(tenths / 10),
                     (unsigned long)(tenths % 10));
    else snprintf(detail, len, "differs: features %d, output %d LSB", max_in, max_out);
    return ok;
}

/* ---------- events ---------- */

static ui_event events[MAX_EVENTS];   /* newest first */
static int n_events;
static int8_t ev_emb[MAX_EVENTS][SS_EMB];   /* each event's latest embedding and profile, for T4 */
static int8_t ev_prof[MAX_EVENTS];

static ui_event *add_event(const look *lk, bool maybe, ui_felt felt, float score, uint32_t t, bool *merged,
                           const float *emb, int p)
{
    ui_event *e = &events[0];
    if (n_events && e->label == lk->label && t - (e->t_ms + e->dur_ms) < MERGE_MS) {
        ss_store_q8(emb, ev_emb[0]);
        ev_prof[0] = (int8_t)p;
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
    memmove(&ev_emb[1], &ev_emb[0], (size_t)(n_events - 1) * SS_EMB);
    memmove(&ev_prof[1], &ev_prof[0], (size_t)(n_events - 1));
    ss_store_q8(emb, ev_emb[0]);
    ev_prof[0] = (int8_t)p;
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

enum screen { READY, HOME, ALERT, TIMELINE, STATS, TEACH, MENU };
static screen scr = READY;
static bool redraw = true;

static ui_home home;
static ui_guess top3[3];
static ui_event alert_ev, banner_ev;
static uint32_t alert_t0, alert_last, banner_t0;
static bool banner_on;
static int alert_phase, tl_first;

/* Night mode (A6): info sounds are only logged; the home screen goes dark (backlight off, no live view,
 * no live encoder) NIGHT_DARK_MS after the last press or alert; important and urgent alerts light it. */
static bool night, dark;
static uint32_t last_wake;

/* C3: time from a decision to the alert on screen, and what drawing costs (draw = into the back buffer,
 * present = copy of the changed rows to the scanned frame buffer) */
static uint32_t c3_alert_cyc;                                  /* decision of an alert not yet on screen */
static uint32_t draw_max_us, present_max_us, n_live, live_max_us;   /* this stats window */
static uint32_t al_draws, al_draw_max_us, al_present_max_us, al_drop0;   /* this alert */

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
    int8_t ex[SHOTS][SS_EMB];
    for (int k = 0; k < SHOTS; k++) ss_store_q8(tc.emb[k], ex[k]);
    profile_add(tc.preset, tc.proto, tc.thr, ex, SHOTS);
    store_save();
    printf("[TEACH] saved '%s' thr %.3f; %d taught\r\n", kPresets[tc.preset].label, (double)tc.thr, n_taught);
    scr = HOME;
    redraw = true;
}

SS_COLD static void alert_end_log(void)
{
    printf("[C3] alert '%s' ended after %lu ms: %lu full redraws, longest draw %lu us + present %lu us, "
           "%lu audio blocks dropped\r\n", alert_ev.label, (unsigned long)(ss_ms() - alert_t0),
           (unsigned long)al_draws, (unsigned long)al_draw_max_us, (unsigned long)al_present_max_us,
           (unsigned long)(kws_audio_dropped() - al_drop0));
}

static void go_home(void)
{
    if (scr == ALERT) {
        alert_end_log();
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
static float active_pct = 100.0f, npu_pct;   /* C5: CPU outside WFI, NPU executing, % of wall time */
static uint32_t sleep_cyc, n_wfi, n_iter;    /* this stats window: cycles in WFI, WFIs, loop passes */
static uint64_t win_npu0;

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
    s->cpu_active_pct = active_pct;
    s->infer_per_s = infer_rate;
    s->npu_busy_pct = npu_pct;
    s->model_kb = ss_encoder_pte_len / 1024;
    s->emb_dim = SS_EMB;
    s->n_builtin = SS_NUM_BUILTIN;
    s->n_taught = n_taught;
    s->uptime_s = ss_ms() / 1000;
    s->dropped = kws_audio_dropped();
    s->save_ms = ss_store_get_stats()->last_ms;
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
    if (e->urgency == UI_INFO && night) return;   /* night: logged in the timeline only, screen stays dark */
    last_wake = ss_ms();
    if (e->urgency == UI_INFO) {
        banner_ev = *e;
        banner_on = true;
        banner_t0 = ss_ms();
        if (scr == HOME) {
            redraw = true;
            c3_alert_cyc = ss_cycles();
        }
        return;
    }
    if (scr == TEACH) return;
    /* the same sound again while its alert is up: refresh it, but keep its escalation clock */
    const bool same = scr == ALERT && alert_ev.label == e->label;
    if (scr == ALERT && !same) alert_end_log();
    const uint32_t unanswered = same ? alert_ev.unanswered_ms : 0;
    alert_ev = *e;
    alert_ev.unanswered_ms = unanswered;
    alert_last = ss_ms();
    c3_alert_cyc = ss_cycles();
    if (!same) {
        alert_t0 = alert_last;
        alert_phase = 0;
        al_draws = al_draw_max_us = al_present_max_us = 0;
        al_drop0 = kws_audio_dropped();
    }
    scr = ALERT;
    redraw = true;
}

/* Classify the sound whose onset was at sample `onset`: called once the window holds it 0.2 s in. */
/* Re-measuring a built-in on the board: embeddings of the next sounds replace its profile. */
#define CAL_MAX 16
static int cal_idx = -1, cal_n;
static float cal_emb[CAL_MAX][SS_EMB];

SS_COLD static void cal_finish(void)
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

static void classify(const float *e, const float *s, float snr, int clipped, uint32_t onset_ms);

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
    classify(e, s, snr, clipped, onset_ms);
}

/* The decision for one sound: its embedding e, scores s, level over the room, and onset time. */
static void classify(const float *e, const float *s, float snr, int clipped, uint32_t onset_ms)
{
    int b2;
    const int b = best_of(s, &b2);
    float mg = 0.0f;
    const ui_felt felt = felt_at(onset_ms, &mg);
    const float cxs = cx_score(b, e);
    const bool veto = s[b] >= prof[b].thr && cxs >= CX_MIN && cxs > s[b];   /* T4: like a sound marked wrong */
    const bool over = s[b] >= prof[b].thr && !veto;
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
    if (veto) printf("[SS] '%s' vetoed: %.2f to a sound marked wrong\r\n", prof[b].lk->label, (double)cxs);
    if (!lk) return;

    bool merged;
    ui_event *ev = add_event(lk, maybe, f, score, onset_ms, &merged, e, lk == prof[knock_prof].lk ? knock_prof : b);
    /* a knock only heard (TV?) is shown as a banner, not a full-screen alert */
    if (lk == prof[knock_prof].lk && f == UI_FELT_HEARD_ONLY) ev->urgency = UI_INFO;
    lat[n_lat++ % 16] = ss_ms() - onset_ms;
    alert_raise(ev);
    if (scr == HOME) redraw = true;
}

/* ---------- fixing mistakes (T4) ---------- */

/* "Right" on a taught sound: its embedding joins the examples (the oldest goes when full), and the
 * profile and threshold are rebuilt from all of them, as when teaching. */
SS_COLD static void taught_add_example(int k, const int8_t *q)
{
    int n = taught_n_ex[k];
    if (n >= SS_STORE_MAX_EX) {
        memmove(taught_ex[k][0], taught_ex[k][1], (size_t)(n - 1) * SS_EMB);
        n--;
    }
    memcpy(taught_ex[k][n++], q, SS_EMB);
    taught_n_ex[k] = (uint8_t)n;
    float emb[SS_STORE_MAX_EX][SS_EMB];
    for (int j = 0; j < n; j++) ss_store_f32(taught_ex[k][j], emb[j]);
    profile &p = prof[SS_NUM_BUILTIN + k];
    const float was = p.thr;
    p.thr = ss_make_profile(emb, n, taught_proto[k], NULL);
    printf("[SS] '%s' marked right: now %d examples, threshold %.3f -> %.3f\r\n", p.lk->label, n, (double)was,
           (double)p.thr);
    store_save();
}

/* The user says event i was right or wrong (from its alert or the timeline). */
SS_COLD static void event_mark(int i, bool right)
{
    if (i < 0 || i >= n_events) return;
    ui_event *ev = &events[i];
    const int p = ev_prof[i];
    if (p < 0 || p >= n_prof || prof[p].lk->label != ev->label) {
        printf("[SS] can't mark '%s': that sound was forgotten\r\n", ev->label);
        return;
    }
    ev->acked = true;
    ev->fixed = right ? 1 : 2;
    if (right) {
        if (p >= SS_NUM_BUILTIN) taught_add_example(p - SS_NUM_BUILTIN, ev_emb[i]);
        else printf("[SS] '%s' marked right\r\n", ev->label);
        return;
    }
    if (n_cx[p] >= SS_STORE_MAX_EX) {   /* full: the oldest goes */
        memmove(cx[p][0], cx[p][1], (SS_STORE_MAX_EX - 1) * SS_EMB);
        n_cx[p]--;
    }
    memcpy(cx[p][n_cx[p]++], ev_emb[i], SS_EMB);
    printf("[SS] '%s' marked wrong: kept as counter-example %d%s\r\n", ev->label, n_cx[p],
           p >= SS_NUM_BUILTIN ? ", saved" : " (built-in: until restart)");
    if (p >= SS_NUM_BUILTIN) store_save();
}

/* Host command 15: the boot test clip (a dog bark) through the encoder and the decision, as if just
 * heard: a silent way to test the whole decision path, counter-examples included. */
SS_COLD static void classify_test_clip(void)
{
    int8_t out[SS_EMB];
    float e[SS_EMB], s[MAX_PROF];
    if (!infer(ss_test_input, out)) return;
    ss_embed(out, e);
    score_all(e, s);
    printf("[SS] command 15: the boot test clip ('%s') as if just heard\r\n", SS_TEST_NAME);
    classify(e, s, 20.0f, 0, ss_ms());
}

/* ---------- managing taught sounds (T7) and the menu ---------- */

/* Forget taught sound k (0-based among the taught ones) and save. */
SS_COLD static void taught_forget(int k)
{
    if (k < 0 || k >= n_taught) return;
    printf("[SS] forgot '%s'\r\n", prof[SS_NUM_BUILTIN + k].lk->label);
    for (int j = k; j + 1 < n_taught; j++) {
        memcpy(taught_proto[j], taught_proto[j + 1], sizeof(taught_proto[j]));
        memcpy(taught_ex[j], taught_ex[j + 1], sizeof(taught_ex[j]));
        taught_n_ex[j] = taught_n_ex[j + 1];
        prof[SS_NUM_BUILTIN + j] = prof[SS_NUM_BUILTIN + j + 1];
        prof[SS_NUM_BUILTIN + j].proto = taught_proto[j];
        memcpy(cx[SS_NUM_BUILTIN + j], cx[SS_NUM_BUILTIN + j + 1], sizeof(cx[0]));
        n_cx[SS_NUM_BUILTIN + j] = n_cx[SS_NUM_BUILTIN + j + 1];
    }
    n_taught--;
    n_prof = SS_NUM_BUILTIN + n_taught;
    store_save();
}

SS_COLD static void taught_forget_all(void)
{
    n_taught = 0;
    n_prof = SS_NUM_BUILTIN;
    store_save();
    printf("[SS] taught sounds cleared\r\n");
}

enum mitem { MI_NIGHT, MI_FORGET, MI_FORGET_ALL, MI_BACK };
static struct {
    mitem kind[UI_MENU_MAX];
    int arg[UI_MENU_MAX];
    int n, sel, confirm;   /* confirm: row waiting for a second press (forgetting), or -1 */
    char text[UI_MENU_MAX][32], note[48];
} menu;

SS_COLD static void menu_build(void)
{
    menu.n = 0;
    menu.kind[menu.n] = MI_NIGHT;
    menu.arg[menu.n++] = 0;
    for (int k = 0; k < n_taught; k++) {
        menu.kind[menu.n] = MI_FORGET;
        menu.arg[menu.n++] = k;
    }
    if (n_taught > 1) {
        menu.kind[menu.n] = MI_FORGET_ALL;
        menu.arg[menu.n++] = 0;
    }
    menu.kind[menu.n] = MI_BACK;
    menu.arg[menu.n++] = 0;
    if (menu.sel >= menu.n) menu.sel = menu.n - 1;
    menu.confirm = -1;
}

SS_COLD static void menu_open(void)
{
    menu.sel = 0;
    menu_build();
    scr = MENU;
    redraw = true;
}

SS_COLD static void menu_choose(void)
{
    const int i = menu.sel;
    switch (menu.kind[i]) {
    case MI_NIGHT:
        night = !night;
        printf("[SS] night mode %s\r\n", night ? "on" : "off");
        break;
    case MI_FORGET:
    case MI_FORGET_ALL:
        if (menu.confirm != i) {   /* forgetting can't be undone: press twice */
            menu.confirm = i;
            return;
        }
        if (menu.kind[i] == MI_FORGET) taught_forget(menu.arg[i]);
        else taught_forget_all();
        menu_build();
        return;
    case MI_BACK:
        scr = HOME;
        break;
    }
    menu.confirm = -1;
}

SS_COLD static void menu_draw(void)
{
    ui_menu m = {};
    m.title = "Settings";
    m.n = menu.n;
    m.sel = menu.sel;
    for (int i = 0; i < menu.n; i++) {
        switch (menu.kind[i]) {
        case MI_NIGHT:
            m.item[i] = "Night mode";
            m.value[i] = night ? "On" : "Off";
            break;
        case MI_FORGET:
            snprintf(menu.text[i], sizeof(menu.text[i]), "Forget %s", prof[SS_NUM_BUILTIN + menu.arg[i]].lk->label);
            m.item[i] = menu.text[i];
            m.value[i] = "taught";
            m.danger[i] = true;
            break;
        case MI_FORGET_ALL:
            snprintf(menu.text[i], sizeof(menu.text[i]), "Forget all %d taught", n_taught);
            m.item[i] = menu.text[i];
            m.danger[i] = true;
            break;
        case MI_BACK:
            m.item[i] = "Back";
            break;
        }
    }
    if (menu.confirm >= 0) {
        const int c = menu.confirm;
        if (menu.kind[c] == MI_FORGET)
            snprintf(menu.note, sizeof(menu.note), "Press again to forget %s", prof[SS_NUM_BUILTIN + menu.arg[c]].lk->label);
        else
            snprintf(menu.note, sizeof(menu.note), "Press again to forget all %d", n_taught);
        m.note = menu.note;
    } else if (menu.kind[menu.sel] == MI_NIGHT) {
        m.note = night ? "Night: screen dark, info sounds only logged" : "Press for night mode";
    }
    ui_menu_draw(&m);
}

/* ---------- host commands and joystick ---------- */

static uint32_t host_keys;
static uint32_t stress_until;   /* host command 14: full redraws 10 times a second until then (C3 row 3) */

/* Host command 13: act as if profile (a & 0xff) had just been recognised, with no sound played, to test
 * alerts silently. Bit 8: "maybe"; bit 9: felt through the table; bit 10: heard only. */
SS_COLD static void inject(uint32_t a)
{
    const int p = (int)(a & 0xffu);
    if (p >= n_prof || (!prof[p].alerting && p != knock_prof)) {
        printf("[SS] command 13: profile %d doesn't alert\r\n", p);
        return;
    }
    const ui_felt f = a & 0x200u ? UI_FELT_YES : a & 0x400u ? UI_FELT_HEARD_ONLY : UI_FELT_NA;
    const uint32_t now = ss_ms();
    bool merged;
    ui_event *ev = add_event(prof[p].lk, (a & 0x100u) != 0, f, prof[p].thr + 0.1f, now, &merged, prof[p].proto, p);
    if (p == knock_prof && f == UI_FELT_HEARD_ONLY) ev->urgency = UI_INFO;
    printf("[SS] t=%lu injected '%s'%s (host command 13, no sound)\r\n", (unsigned long)now, prof[p].lk->label,
           a & 0x100u ? " maybe" : "");
    alert_raise(ev);
    if (scr == HOME) redraw = true;
}

/* 1 teach preset ss_arg (straight to recording), 2 save the taught sound, 3 home / cancel,
 * 4 forget taught sounds, 5 stats, 6 timeline, 7 clock (ss_arg = ms after midnight now),
 * 8 press joystick keys (ss_arg = SS_JOY_* bits), 9 re-measure built-in ss_arg from the next sounds,
 * 10 finish re-measuring, 11 save to flash ss_arg times back to back (C2 row 1), 12 fill every taught
 * slot with a stand-in (C2 bench, full-size record), 13 fake a recognised sound (see inject()),
 * 14 redraw the whole screen 10 times a second for ss_arg seconds (C3 row 3), 15 classify the boot test
 * clip as if heard, 16 mark timeline event (ss_arg & 0xff) wrong, or right with bit 8 (T4). */
static void host_command(void)
{
    const uint32_t c = ss_cmd, a = ss_arg;
    if (!c) return;
    ss_cmd = 0;
    switch (c) {
    case 1: teach_open((int)(a % N_PRESETS), true); break;
    case 2: teach_save(); break;
    case 3: go_home(); break;
    case 4: taught_forget_all(); break;
    case 5: scr = STATS; redraw = true; break;
    case 6: scr = TIMELINE; tl_first = 0; redraw = true; break;
    case 7: ui_set_clock(a - ss_ms()); redraw = true; break;
    case 8: host_keys |= a; break;
    case 9: cal_finish(); cal_idx = (int)(a % SS_NUM_BUILTIN); cal_n = 0;
            printf("[CAL] '%s': measuring\r\n", ss_builtin_names[cal_idx]); break;
    case 10: cal_finish(); break;
    case 11: store_repeat = a ? a : 1; store_save(); break;
    case 12: store_fill_test(); break;
    case 13: inject(a); break;
    case 14: stress_until = ss_ms() + 1000u * (a ? a : 10); break;
    case 15: classify_test_clip(); break;
    case 16: event_mark((int)(a & 0xffu), (a & 0x100u) != 0); redraw = true; break;
    }
}

static void on_keys(uint32_t k)
{
    switch (scr) {
    case READY:
        go_home();
        break;
    case HOME:
        if (k & SS_JOY_UP) teach_open(0, false);
        else if (k & SS_JOY_LEFT) { scr = TIMELINE; tl_first = 0; redraw = true; }
        else if (k & SS_JOY_RIGHT) { scr = STATS; redraw = true; }
        else if (k & SS_JOY_DOWN) menu_open();
        else if (k & SS_JOY_CENTER) { banner_on = false; redraw = true; }
        break;
    case MENU:
        if (k & SS_JOY_UP) { if (menu.sel > 0) menu.sel--; menu.confirm = -1; }
        else if (k & SS_JOY_DOWN) { if (menu.sel + 1 < menu.n) menu.sel++; menu.confirm = -1; }
        else if (k & (SS_JOY_LEFT | SS_JOY_RIGHT)) scr = HOME;
        else if (k & SS_JOY_CENTER) menu_choose();
        redraw = true;
        break;
    case ALERT:   /* LEFT: wrong sound, RIGHT: right (T4); the alert's event is the newest with its label */
        if (k & (SS_JOY_LEFT | SS_JOY_RIGHT) && !alert_ev.unanswered_ms)
            for (int i = 0; i < n_events; i++)
                if (events[i].label == alert_ev.label) {
                    event_mark(i, (k & SS_JOY_RIGHT) != 0);
                    break;
                }
        go_home();
        break;
    case TIMELINE:   /* the top row is the selected one: LEFT wrong, RIGHT right (T4) */
        if (k & SS_JOY_UP) { if (tl_first > 0) tl_first--; }
        else if (k & SS_JOY_DOWN) { if (tl_first + 1 < n_events) tl_first++; }
        else if (k & (SS_JOY_LEFT | SS_JOY_RIGHT)) event_mark(tl_first, (k & SS_JOY_RIGHT) != 0);
        else { go_home(); break; }
        redraw = true;
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

/* ---------- boot self-test (S1) ---------- */

static ui_ready ready;
static uint32_t ready_t;
static bool ready_fail;

SS_COLD static void ready_set(int i, const char *name, ui_check_state st, const char *detail)
{
    ready.item[i].name = name;
    ready.item[i].state = st;
    snprintf(ready.item[i].detail, sizeof(ready.item[i].detail), "%s", detail);
    if (i >= ready.n) ready.n = i + 1;
    if (st == UI_CHECK_FAIL) ready_fail = true;
}

/* Both mics over 0.5 s, after 0.2 s to settle: alive (not digital silence), not pinned at full scale, within 20 dB of each
 * other. Channel 0 is mic 4 (left), channel 1 mic 5 (right). */
static struct { float ss[2]; int peak[2]; uint32_t n; } micq;

static void mic_tap(const int16_t *st, int n, uint32_t)
{
    for (int i = 0; i < n; i++)
        for (int c = 0; c < 2; c++) {
            const int v = st[2 * i + c], a = v < 0 ? -v : v;
            micq.ss[c] += (float)v * (float)v;
            if (a > micq.peak[c]) micq.peak[c] = a;
        }
    micq.n += (uint32_t)n;
}

SS_COLD static ui_check_state mic_check(char *detail, size_t len)
{
    memset(&micq, 0, sizeof(micq));
    const uint32_t t0 = ss_ms();
    uint32_t t_imu = t0;
    while (ss_ms() - t0 < 700) {
        if (ss_ms() - t0 >= 200) ss_audio_set_tap(mic_tap);   /* after the mics' start-up surge */
        kws_audio_poll();
        if (imu_ok && ss_ms() - t_imu >= 5) {   /* its FIFO holds about 300 ms */
            t_imu = ss_ms();
            ss_imu_poll();
        }
    }
    ss_audio_set_tap(NULL);
    ss_onset o;
    ss_knock k;
    while (ss_audio_onset(&o)) {}   /* start-up noises aren't sounds to report */
    while (ss_imu_knock(&k)) {}
    if (micq.n == 0) { snprintf(detail, len, "no audio from the microphones"); return UI_CHECK_FAIL; }
    float db[2];
    for (int c = 0; c < 2; c++) db[c] = 10.0f * log10f(micq.ss[c] / micq.n / (32768.0f * 32768.0f) + 1e-12f);
    printf("[S1] mics: left %.1f dB (peak %d), right %.1f dB (peak %d) over %lu samples\r\n", (double)db[0],
           micq.peak[0], (double)db[1], micq.peak[1], (unsigned long)micq.n);
    static const char *const side[2] = {"left", "right"};
    for (int c = 0; c < 2; c++)
        if (db[c] < -90.0f) { snprintf(detail, len, "%s mic silent", side[c]); return UI_CHECK_FAIL; }
    for (int c = 0; c < 2; c++)
        if (db[c] > -6.0f) { snprintf(detail, len, "%s mic stuck loud (%d dB)", side[c], (int)db[c]); return UI_CHECK_WARN; }
    const float d = db[0] - db[1];
    if (d > 20.0f || d < -20.0f) {
        snprintf(detail, len, "%s mic %d dB quieter", side[d > 0], (int)(d > 0 ? d : -d));
        return UI_CHECK_WARN;
    }
    snprintf(detail, len, "left %d dB, right %d dB", (int)lrintf(db[0]), (int)lrintf(db[1]));
    return UI_CHECK_OK;
}

/* ---------- power (C5) ---------- */

/* The cycle counter times everything (onsets, knocks, latency), so the loop only sleeps if it keeps
 * counting through WFI. */
SS_COLD static bool dwt_runs_in_wfi(void)
{
    const uint32_t m0 = ss_ms();
    while (ss_ms() == m0) {}
    const uint32_t c0 = ss_cycles(), m1 = ss_ms();
    while (ss_ms() - m1 < 20) __WFI();
    const float ratio = (float)(ss_cycles() - c0) / ((float)(ss_ms() - m1) * (SystemCoreClock / 1000u));
    printf("[C5] cycle counter through WFI: %.0f%% of wall time -> %s\r\n", (double)(ratio * 100.0f),
           ratio > 0.9f ? "sleeping between audio blocks" : "no sleep (timing would drift)");
    return ratio > 0.9f;
}

/* ---------- drawing ---------- */

static void draw(uint32_t now, bool live_only)
{
    const uint32_t t0 = ss_cycles();
    switch (scr) {
    case READY:
        ui_ready_draw(&ready);
        break;
    case HOME:
        home.listening = true;
        home.mode = night ? "Night" : "Home";
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
    case MENU:
        menu_draw();
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
    const uint32_t t1 = ss_cycles();
    present_dirty();
    const uint32_t t2 = ss_cycles();
    work_cyc += t2 - t0;
    const uint32_t d_us = ss_cyc_to_us(t1 - t0), p_us = ss_cyc_to_us(t2 - t1);
    if (live_only) {
        n_live++;
        if (d_us + p_us > live_max_us) live_max_us = d_us + p_us;
        return;
    }
    if (d_us > draw_max_us) draw_max_us = d_us;
    if (p_us > present_max_us) present_max_us = p_us;
    if (scr == ALERT) {
        al_draws++;
        if (d_us > al_draw_max_us) al_draw_max_us = d_us;
        if (p_us > al_present_max_us) al_present_max_us = p_us;
    }
    if (c3_alert_cyc && (scr == ALERT || (scr == HOME && banner_on))) {
        printf("[C3] %s on screen %lu us after the decision (draw %lu us, present %lu us)\r\n",
               scr == ALERT ? "alert" : "banner", (unsigned long)ss_cyc_to_us(t2 - c3_alert_cyc),
               (unsigned long)d_us, (unsigned long)p_us);
        c3_alert_cyc = 0;
    }
}

static void set_leds(uint32_t now)
{
    ui_led led[2] = {};
    if (scr == ALERT) ui_led_pattern(alert_ev.urgency, alert_ev.cat, alert_ev.maybe, now - alert_t0, led);
    else if (banner_on) ui_led_pattern(UI_INFO, banner_ev.cat, banner_ev.maybe, now - banner_t0, led);
    else if (scr == TEACH && tc.ph != T_NAMING) led[0].g = (now / 500) & 1;   /* slow green: recording */
    else if (scr == READY && ready_fail) led[0].r = led[1].r = (now / 500) & 1;   /* slow red: not ready */
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
    const bool led_ok = ss_led_init() == 0;
    if (!led_ok) printf("[SS] LED init failed\r\n");
    printf("\r\n[SS] Sound Sense starting\r\n");

    /* the "Ready" screen fills in as the checks run (S1) */
    ui_init(ss_gfx_back());
    ready_set(0, "Microphones", UI_CHECK_RUNNING, "");
    ready_set(1, "Neural processor", UI_CHECK_RUNNING, "");
    ready_set(2, "Saved sounds", UI_CHECK_RUNNING, "");
    ready_set(3, "Vibration sensor", UI_CHECK_RUNNING, "");
    ready_set(4, "Display and lights", led_ok ? UI_CHECK_OK : UI_CHECK_WARN, led_ok ? "ok" : "lights not answering");
    draw(ss_ms(), false);

    char d[48];
    if (executorch_init(ss_encoder_pte, ss_encoder_pte_len) != 0) {
        ready_set(1, "Neural processor", UI_CHECK_FAIL, "runtime didn't start");
        draw(ss_ms(), false);
        ss_halt("executorch_init");
    }
    executorch_set_verbose(0);
    profiles_init();
    const bool npu_ok = self_test(d, sizeof(d));
    ready_set(1, "Neural processor", npu_ok ? UI_CHECK_OK : UI_CHECK_FAIL, d);
    draw(ss_ms(), false);

    const int sr = store_load();
    snprintf(d, sizeof(d), "%d taught sound%s restored", n_taught, n_taught == 1 ? "" : "s");
    ready_set(2, "Saved sounds", sr < 0 ? UI_CHECK_WARN : UI_CHECK_OK,
              sr < 0 ? "flash not answering: won't be kept" : n_taught ? d : "nothing taught yet");
    draw(ss_ms(), false);

    const int ir = ss_imu_init();
    imu_ok = ir == 0;
    if (!imu_ok) printf("[SS] IMU init failed (%d): knocks can't be felt\r\n", ir);
    ready_set(3, "Vibration sensor", imu_ok ? UI_CHECK_OK : UI_CHECK_WARN,
              imu_ok ? "knocks can be felt" : "not found: knocks heard only");
    draw(ss_ms(), false);

    ss_audio_init();
    if (kws_audio_start() != 0) {
        ready_set(0, "Microphones", UI_CHECK_FAIL, "didn't start");
        draw(ss_ms(), false);
        ss_halt("microphone start");
    }
    const ui_check_state mic = mic_check(d, sizeof(d));
    ready_set(0, "Microphones", mic, d);
    ready.done = true;
    ready.boot_ms = ready_t = ss_ms();
    draw(ss_ms(), false);
    printf("[S1] %s after %lu ms: mics %s; npu %s; saved sounds %s; imu %s; lights %s\r\n",
           ready_fail ? "NOT READY" : "ready", (unsigned long)ready.boot_ms, ready.item[0].detail,
           ready.item[1].detail, ready.item[2].detail, ready.item[3].detail, ready.item[4].detail);
    const bool use_wfi = dwt_runs_in_wfi();
    printf("[SS] listening; %d built-in sounds\r\n", SS_NUM_BUILTIN);

    static int8_t in[IN_LEN];
    int8_t out[SS_EMB];
    float e[SS_EMB], s[MAX_PROF];
    int frames = 0;
    bool pend = false;
    uint32_t pend_sample = 0, pend_ms = 0, last_onset = 0;
    bool any_onset = false;
    uint32_t last_imu = 0, last_live_draw = 0, last_slow = 0, last_log = 0, last_key = 0;
    uint32_t win_ms0 = ss_ms(), last_loud = 0, last_joy = 0, last_stress = 0;
    bool any_loud = false;

    for (;;) {
        n_iter++;
        const int got = kws_audio_poll();
        frames += got;
        const uint32_t now = ss_ms();
        if (got && ss_audio_block_db() > ss_audio_floor_db() + GATE_DB) {
            last_loud = now;
            any_loud = true;
        }
        host_command();

        if (imu_ok && now - last_imu >= 10) {   /* FIFO holds ~300 ms; each read busy-waits on I3C (C5) */
            last_imu = now;
            ss_imu_poll();
            ss_knock k;
            while (ss_imu_knock(&k)) {
                knock_ms[n_knocks % 8] = ev_ms(k.cyc);
                knock_mg[n_knocks % 8] = k.peak_mg;
                n_knocks++;
            }
        }

        /* joystick every 10 ms (a low-power GPIO read is slow: reading it every pass kept the CPU 2/3 busy),
         * with a 250 ms lock-out against switch bounce */
        uint32_t keys = host_keys;
        host_keys = 0;
        if (now - last_joy >= 10) {
            last_joy = now;
            keys |= ss_joy_pressed();
        }
        if (keys && now - last_key >= 250) {
            last_key = now;
            last_key_ms = now;
            last_wake = now;
            if (!dark) on_keys(keys);   /* in the dark, a press only lights the screen */
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

        /* live view: spectrogram columns every 100 ms; the encoder and best three matches only on the
         * home screen while the room is above its noise floor (E1) */
        if (frames >= LIVE_EVERY) {
            const int n_new = frames > KWS_FRAMES ? KWS_FRAMES : frames;
            frames = 0;
            const uint32_t t0 = ss_cycles();
            kws_make_input(in, SS_IN_SCALE, SS_IN_ZP);
            feat_us = ss_cyc_to_us(ss_cycles() - t0);
            const bool npu_on = scr == HOME && !dark && any_loud && now - last_loud < GATE_HOLD_MS;
            if (npu_on) {
                if (!infer(in, out)) ss_halt("inference");
                ss_embed(out, e);
                score_all(e, s);
            }
            work_cyc += ss_cycles() - t0;
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
            for (int i = 0; i < 3 && !npu_on; i++) top3[i] = {i ? NULL : "Quiet: NPU resting", 0, false};
            for (int i = 0; i < 3 && npu_on; i++) {
                int b = -1;
                for (int p = 0; p < n_prof; p++)
                    if (!used[p] && (b < 0 || s[p] > s[b])) b = p;
                if (b < 0) { top3[i] = {"", 0, false}; continue; }
                used[b] = true;
                top3[i].label = prof[b].lk->label;
                top3[i].score = (uint8_t)(s[b] < 0 ? 0 : s[b] * 100);
                top3[i].over_threshold = s[b] >= prof[b].thr;
            }
            if (scr == HOME && !redraw && !dark && now - last_live_draw >= 100) {
                last_live_draw = now;
                draw(now, true);
            }
        }

        /* timeouts, the urgent strobe (one flash a second) and slow refreshes */
        if (scr == READY && !ready_fail && now - ready_t > READY_MS) go_home();
        if (banner_on && now - banner_t0 > BANNER_MS) {
            banner_on = false;
            if (scr == HOME) redraw = true;
        }
        if (scr == ALERT) {
            const bool urgent = alert_ev.urgency == UI_URGENT && !alert_ev.maybe;
            const uint32_t up = now - alert_t0;
            if (!urgent && now - alert_last > ALERT_MS) {
                alert_end_log();
                scr = HOME;
                redraw = true;
            } else if (urgent) {
                /* A5: an urgent alert stays until someone presses; after 30 s the strobe goes from one flash a
                 * second to two (still under 3 Hz, the photosensitivity limit) and the screen says how long */
                if (up >= ESCALATE_MS && !alert_ev.unanswered_ms)
                    printf("[SS] urgent '%s' unanswered for %lu s: escalating\r\n", alert_ev.label,
                           (unsigned long)(up / 1000));
                if (up >= ESCALATE_MS && up / 1000 != alert_ev.unanswered_ms / 1000) {
                    alert_ev.unanswered_ms = up;
                    redraw = true;
                }
                const uint32_t half = alert_ev.unanswered_ms ? 250 : 500;
                if ((int)(up / half % 2) != alert_phase) {
                    alert_phase ^= 1;
                    redraw = true;
                }
            }
        }
        /* night mode: dark on the home screen after a while; a press or an alert lights it again */
        const bool want_dark = night && scr == HOME && !banner_on && now - last_wake >= NIGHT_DARK_MS;
        if (want_dark != dark) {
            dark = want_dark;
            ss_backlight(!dark);
            printf("[SS] screen %s\r\n", dark ? "dark (night mode)" : "lit");
            if (!dark) redraw = true;
        }
        if (now - last_slow >= 250) {
            last_slow = now;
            if (scr == TEACH || (scr == STATS && now % 1000 < 250)) redraw = true;
        }
        if ((int32_t)(stress_until - now) > 0 && now - last_stress >= 100) {
            last_stress = now;
            redraw = true;
        }
        if (redraw) {
            redraw = false;
            draw(now, false);
        } else if (ss_store_busy()) {
            store_step();   /* not in the same pass as a full redraw: together they could outlast the audio ring */
        }
        set_leds(now);

        if (now - last_log >= 10000) {
            last_log = now;
            const float win_cyc = (float)(now - win_ms0) * (SystemCoreClock / 1000u);
            cpu_pct = 100.0f * work_cyc / win_cyc;
            active_pct = 100.0f * (1.0f - sleep_cyc / win_cyc);
            npu_pct = 100.0f * (float)(npu_sum_us - win_npu0) / ((float)(now - win_ms0) * 1000.0f);
            infer_rate = (n_infer - win_infer0) * 1000.0f / (float)(now - win_ms0);
            const uint32_t passes_s = n_iter * 1000u / (now - win_ms0), sleeps_s = n_wfi * 1000u / (now - win_ms0);
            const uint32_t sleep_us = n_wfi ? ss_cyc_to_us(sleep_cyc / n_wfi) : 0;
            const uint32_t live_fps10 = n_live * 10000u / (now - win_ms0);
            printf("[SS] draw: full max %lu us + present %lu us; live view %lu.%lu fps, max %lu us\r\n",
                   (unsigned long)draw_max_us, (unsigned long)present_max_us, (unsigned long)(live_fps10 / 10),
                   (unsigned long)(live_fps10 % 10), (unsigned long)live_max_us);
            draw_max_us = present_max_us = n_live = live_max_us = 0;
            work_cyc = sleep_cyc = n_wfi = n_iter = 0;
            win_npu0 = npu_sum_us;
            win_infer0 = n_infer;
            win_ms0 = now;
            const ss_imu_stats *is = ss_imu_get_stats();
            printf("[SS] stats t=%lu level %d dB floor %d dB, npu %lu us runner %lu us feat %lu us, %.1f inf/s, "
                   "busy %.0f%%, active %.1f%% (%lu passes/s, %lu sleeps/s, %lu us each), npu busy %.2f%%, dropped %lu, imu %lu samples %lu errors (times back %lu us, clock %+ld ppm), knocks %lu, events %d, saves %lu\r\n",
                   (unsigned long)now, (int)ss_audio_block_db(), (int)ss_audio_floor_db(), (unsigned long)npu_us,
                   (unsigned long)runner_us, (unsigned long)feat_us, (double)infer_rate, (double)cpu_pct,
                   (double)active_pct, (unsigned long)passes_s, (unsigned long)sleeps_s, (unsigned long)sleep_us,
                   (double)npu_pct,
                   (unsigned long)kws_audio_dropped(), (unsigned long)(imu_ok ? is->samples : 0),
                   (unsigned long)(imu_ok ? is->errors : 0), (unsigned long)(imu_ok ? is->back_max_us : 0),
                   (long)(imu_ok ? is->clock_ppm : 0),
                   (unsigned long)n_knocks, n_events,
                   (unsigned long)ss_store_get_stats()->saves);
        }

        /* nothing waiting: sleep until the next interrupt (SysTick every 1 ms, or the mics) (E2) */
        if (use_wfi && !got && !redraw && !ss_store_busy()) {
            const uint32_t c0 = ss_cycles();
            __WFI();
            sleep_cyc += ss_cycles() - c0;
            n_wfi++;
        }
    }
}
