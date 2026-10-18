/* Sound Sense screens (docs/SOUND-SENSE.md §6).
 *
 * The UI only draws: the app owns the state and passes it in. Each screen has a full draw, and
 * the home screen also has a cheap update for its live parts. After drawing, gfx_dirty_rows()
 * says which rows to copy to the scanned frame buffer.
 *
 * Design rules (SOUND-SENSE.md §1, X1): icon + word on every alert, never colour alone, colours
 * from the colour-blind-safe Okabe-Ito palette, urgent strobe at no more than 1 flash per second
 * (the app toggles `phase` every 500 ms). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { UI_INFO, UI_IMPORTANT, UI_URGENT } ui_urgency;

/* Colour groups. */
typedef enum {
    UI_CAT_SAFETY,     /* smoke, CO, glass, siren */
    UI_CAT_DOOR,       /* knock, doorbell, intercom */
    UI_CAT_FAMILY,     /* baby, name called */
    UI_CAT_ANIMAL,     /* dog */
    UI_CAT_APPLIANCE,  /* microwave, kettle, washer, phone */
    UI_CAT_WATER,      /* running water */
    UI_CAT_VOICE,      /* speech nearby */
    UI_CAT_OTHER,      /* unknown, custom */
    UI_CAT_COUNT
} ui_category;

typedef enum {
    UI_ICON_KNOCK, UI_ICON_DOORBELL, UI_ICON_DOG, UI_ICON_BABY, UI_ICON_ALARM, UI_ICON_SMOKE,
    UI_ICON_CO, UI_ICON_GLASS, UI_ICON_SIREN, UI_ICON_WATER, UI_ICON_SPEECH, UI_ICON_NAME,
    UI_ICON_MICROWAVE, UI_ICON_KETTLE, UI_ICON_WASHER, UI_ICON_PHONE, UI_ICON_UNKNOWN,
    UI_ICON_CUSTOM, UI_ICON_COUNT
} ui_icon;

typedef enum { UI_FELT_NA, UI_FELT_YES, UI_FELT_HEARD_ONLY } ui_felt;

typedef struct {
    const char *label;     /* "Doorbell" */
    ui_icon icon;
    ui_category cat;
    ui_urgency urgency;
    bool maybe;            /* low-confidence match: shown as "Maybe: doorbell?" */
    ui_felt felt;          /* knock fusion result (F1) */
    uint8_t confidence;    /* % */
    uint16_t count;        /* repeats merged into this entry (1 = once) */
    uint32_t t_ms;         /* first occurrence, ms since boot */
    uint32_t dur_ms;       /* how long it went on; 0 for a short sound */
    bool acked;
    uint32_t unanswered_ms;  /* urgent alert on screen this long with no press (A5); 0 until it escalates */
    uint8_t fixed;           /* T4: 1 marked right, 2 marked wrong */
} ui_event;

typedef struct {
    const char *label;
    uint8_t score;         /* match score, % */
    bool over_threshold;
} ui_guess;

typedef struct {
    bool listening;        /* false = mic muted */
    const char *mode;      /* "Home", "Night", "Away" */
    float room_db;         /* level of the latest block, dBFS */
    float floor_db;        /* calibrated noise floor, dBFS */
    ui_guess top[3];       /* best matches right now */
    const ui_event *recent;  /* newest first */
    int n_recent;
} ui_home;

typedef enum { UI_SHOT_WAITING, UI_SHOT_OK, UI_SHOT_BAD } ui_shot_state;

#define UI_TEACH_MAX 8
typedef struct {
    const char *label;
    ui_icon icon;
    ui_category cat;
    int n_shots;                        /* examples wanted, usually 5 */
    ui_shot_state shot[UI_TEACH_MAX];
    int8_t shot_db[UI_TEACH_MAX];       /* dB above the noise floor */
    const char *shot_note[UI_TEACH_MAX];/* "ok", "too quiet", "clipped", "unlike the others" */
    float level_db, floor_db;           /* live level */
    int test_score;                     /* -1 until tested, else match % */
} ui_teach;

typedef struct {
    uint32_t npu_us, runner_us, feat_us;
    uint32_t latency_med_ms, latency_p90_ms;
    float cpu_active_pct, npu_busy_pct, infer_per_s;
    uint32_t model_kb;
    int emb_dim, n_builtin, n_taught;
    uint32_t save_ms, uptime_s, dropped;
} ui_stats;

/* Boot self-test (S1): one row per check, then "Ready" or the reason it isn't. */
typedef enum { UI_CHECK_RUNNING, UI_CHECK_OK, UI_CHECK_WARN, UI_CHECK_FAIL } ui_check_state;
#define UI_READY_MAX 6
typedef struct {
    const char *name;      /* "Microphones" */
    char detail[48];       /* "left -52 dB, right -50 dB" */
    ui_check_state state;
} ui_check;
typedef struct {
    ui_check item[UI_READY_MAX];
    int n;
    bool done;             /* every check finished */
    uint32_t boot_ms;      /* power-on to ready */
} ui_ready;

void ui_init(uint8_t (*canvas)[GFX_W][3]);

/* Wall clock (L5): time of day, in ms after midnight, at boot. Until set, times are relative. */
void ui_set_clock(uint32_t ms_after_midnight_at_boot);

/* Home: status, live spectrogram, top matches, level, recent events. */
void ui_home_draw(const ui_home *h, uint32_t now_ms);
void ui_home_update_live(const ui_home *h);         /* spectrogram, level and top matches only */
void ui_spectro_push(const uint8_t col[40]);        /* one 20 ms mel column, 0 (quiet) .. 255 */

/* Full-screen alert for important and urgent sounds; `phase` alternates the urgent strobe. */
void ui_alert_draw(const ui_event *e, uint32_t now_ms, int phase);
/* Info-level sounds: a banner across the top of the home screen. */
void ui_banner_draw(const ui_event *e, uint32_t now_ms);

/* `first` is the top row and the selected one (marked right or wrong with LEFT / RIGHT). */
void ui_timeline_draw(const ui_event *ev, int n, uint32_t now_ms, int first);
void ui_teach_draw(const ui_teach *t);
void ui_stats_draw(const ui_stats *s);
/* A list to pick from (settings, managing sounds): UP/DOWN moves `sel`, press chooses. */
#define UI_MENU_MAX 12
typedef struct {
    const char *title;
    const char *item[UI_MENU_MAX];   /* "Night mode" */
    const char *value[UI_MENU_MAX];  /* shown on the right ("On"), or NULL */
    bool danger[UI_MENU_MAX];        /* destructive (forget a sound): red text */
    int n, sel;
    const char *note;                /* highlighted line above the footer, or NULL */
} ui_menu;
void ui_menu_draw(const ui_menu *m);

/* Any failed check: "Not ready" and the first failure; else "Ready" once done (warnings shown). */
void ui_ready_draw(const ui_ready *r);

/* Shared pieces, also handy for other screens. */
gfx_color ui_cat_color(ui_category c);
/* Icon in fg, centred, inside a size x size box; bg is used for cut-outs (eyes, glass crack). */
void ui_icon_draw(ui_icon icon, float cx, float cy, float size, gfx_color fg, gfx_color bg);

#ifdef __cplusplus
}
#endif
