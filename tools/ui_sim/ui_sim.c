/* Host preview of the Sound Sense screens: renders each one with made-up data to a PNG.
 *
 *   make -C tools/ui_sim        then open tools/ui_sim/out/
 *
 * Uses the same gfx.c, ui.c and ui_led.c that the firmware builds. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <zlib.h>

#include "ui.h"
#include "ui_led.h"

static uint8_t canvas[GFX_H][GFX_W][3];

/* ---------- PNG writer (8-bit RGB, one IDAT) ---------- */

static void be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t n)
{
    uint8_t hdr[8];
    be32(hdr, n);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (n) fwrite(data, 1, n, f);
    uLong crc = crc32(0, (const Bytef *)type, 4);
    if (n) crc = crc32(crc, data, n);
    uint8_t c[4];
    be32(c, (uint32_t)crc);
    fwrite(c, 1, 4, f);
}

/* rgb is w*h*3 bytes in R, G, B order */
static int write_png(const char *path, const uint8_t *rgb, int w, int h)
{
    const size_t raw_n = (size_t)h * (w * 3 + 1);
    uint8_t *raw = malloc(raw_n);
    for (int y = 0; y < h; y++) {
        raw[y * (w * 3 + 1)] = 0;   /* filter: none */
        memcpy(raw + y * (w * 3 + 1) + 1, rgb + (size_t)y * w * 3, (size_t)w * 3);
    }
    uLongf z_n = compressBound(raw_n);
    uint8_t *z = malloc(z_n);
    compress2(z, &z_n, raw, raw_n, 6);

    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); free(raw); free(z); return -1; }
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    be32(ihdr, w);
    be32(ihdr + 4, h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (uint32_t)z_n);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
    return 0;
}

/* ---------- contact sheet ---------- */

#define SHEET_COLS 5
#define SHEET_ROWS 2
#define THUMB_W (GFX_W / 2)
#define THUMB_H (GFX_H / 2)
#define GAP 16
#define SHEET_W (SHEET_COLS * THUMB_W + (SHEET_COLS + 1) * GAP)
#define SHEET_H (SHEET_ROWS * THUMB_H + (SHEET_ROWS + 1) * GAP)
static uint8_t sheet[SHEET_H][SHEET_W][3];
static int n_shots;

static void save(const char *name)
{
    /* the canvas is B, G, R like the panel */
    static uint8_t rgb[GFX_H][GFX_W][3];
    for (int y = 0; y < GFX_H; y++)
        for (int x = 0; x < GFX_W; x++) {
            rgb[y][x][0] = canvas[y][x][2];
            rgb[y][x][1] = canvas[y][x][1];
            rgb[y][x][2] = canvas[y][x][0];
        }
    char path[256];
    snprintf(path, sizeof path, "out/%s.png", name);
    write_png(path, &rgb[0][0][0], GFX_W, GFX_H);

    int y0, y1;
    if (gfx_dirty_rows(&y0, &y1)) printf("%-18s rows %3d..%3d\n", name, y0, y1);
    gfx_dirty_reset();

    if (n_shots < SHEET_COLS * SHEET_ROWS) {   /* 2x box downscale into the sheet */
        const int ox = GAP + (n_shots % SHEET_COLS) * (THUMB_W + GAP);
        const int oy = GAP + (n_shots / SHEET_COLS) * (THUMB_H + GAP);
        for (int y = 0; y < THUMB_H; y++)
            for (int x = 0; x < THUMB_W; x++)
                for (int k = 0; k < 3; k++)
                    sheet[oy + y][ox + x][k] = (uint8_t)((rgb[2 * y][2 * x][k] + rgb[2 * y][2 * x + 1][k] +
                                                          rgb[2 * y + 1][2 * x][k] + rgb[2 * y + 1][2 * x + 1][k] + 2) / 4);
    }
    n_shots++;
}

/* ---------- fake data ---------- */

#define MIN 60000u
#define NOW (3 * 3600000u + 25 * MIN)

static const ui_event events[] = {
    {"Doorbell", UI_ICON_DOORBELL, UI_CAT_DOOR, UI_IMPORTANT, false, UI_FELT_NA, 94, 2, NOW - 2 * MIN, 0, false, 0, 0},
    {"Knock at the door", UI_ICON_KNOCK, UI_CAT_DOOR, UI_IMPORTANT, false, UI_FELT_YES, 88, 3, NOW - 9 * MIN, 0, true, 0, 0},
    {"Baby crying", UI_ICON_BABY, UI_CAT_FAMILY, UI_IMPORTANT, false, UI_FELT_NA, 91, 1, NOW - 34 * MIN, 6 * MIN, true, 0, 0},
    {"Kettle", UI_ICON_KETTLE, UI_CAT_APPLIANCE, UI_INFO, false, UI_FELT_NA, 79, 1, NOW - 51 * MIN, 0, true, 0, 0},
    {"Dog barking", UI_ICON_DOG, UI_CAT_ANIMAL, UI_INFO, true, UI_FELT_NA, 58, 4, NOW - 70 * MIN, 40000, true, 0, 0},
    {"Smoke alarm", UI_ICON_SMOKE, UI_CAT_SAFETY, UI_URGENT, false, UI_FELT_NA, 97, 1, NOW - 95 * MIN, 25000, true, 0, 0},
    {"Water running", UI_ICON_WATER, UI_CAT_WATER, UI_INFO, false, UI_FELT_NA, 83, 1, NOW - 130 * MIN, 12 * MIN, true, 0, 0},
    {"Microwave done", UI_ICON_MICROWAVE, UI_CAT_APPLIANCE, UI_INFO, false, UI_FELT_NA, 86, 1, NOW - 150 * MIN, 0, true, 0, 0},
    {"Sam's phone", UI_ICON_PHONE, UI_CAT_APPLIANCE, UI_INFO, false, UI_FELT_NA, 81, 1, NOW - 160 * MIN, 0, true, 0, 0},
};
#define N_EVENTS ((int)(sizeof events / sizeof events[0]))

/* Spectrogram: a quiet room with a two-tone doorbell and some speech. */
static void fill_spectrogram(void)
{
    srand(7);
    for (int c = 0; c < 416; c++) {
        uint8_t col[40];
        for (int b = 0; b < 40; b++) {
            float v = 30.0f + 18.0f * (rand() / (float)RAND_MAX) - b * 0.4f;        /* noise floor */
            if (c > 60 && c < 150 && (c / 9) % 3 != 0 && b > 4 && b < 22)            /* speech */
                v += 90.0f * expf(-fabsf(b - 10.0f - 3.0f * sinf(c * 0.3f)) / 3.0f) * (0.6f + 0.4f * sinf(c * 0.7f));
            const int t = c - 300;                                                   /* ding-dong */
            if (t > 0 && t < 90) {
                const float env = expf(-(t % 45) / 18.0f);
                const float f0 = t < 45 ? 24.0f : 19.0f;
                v += 210.0f * env * (expf(-fabsf(b - f0)) + 0.6f * expf(-fabsf(b - f0 - 9.0f)));
            }
            col[b] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
        ui_spectro_push(col);
    }
}

static void icon_sheet(void)
{
    gfx_fill(0, 0, GFX_W, GFX_H, (gfx_color){12, 15, 21});
    for (int i = 0; i < UI_ICON_COUNT; i++) {
        const float cx = 80.0f + (i % 4) * 106.0f, cy = 90.0f + (i / 4) * 150.0f;
        const gfx_color bg = ui_cat_color((ui_category)(i % UI_CAT_COUNT));
        gfx_circle(cx, cy, 46, bg);
        const float lum = (0.2126f * bg.r + 0.7152f * bg.g + 0.0722f * bg.b) / 255.0f;
        const gfx_color ink = lum > 0.45f ? (gfx_color){12, 15, 21} : (gfx_color){255, 255, 255};
        ui_icon_draw((ui_icon)i, cx, cy, 58, ink, bg);
    }
}

static void led_trace(void)
{
    /* 4 s of each LED pattern at 10 ms steps, as text bars: '#' = lit */
    static const char *names[] = {"info", "important", "urgent", "maybe"};
    for (int k = 0; k < 4; k++) {
        char row[2][81];
        int changes = 0;
        bool prev = false;
        for (int i = 0; i < 400; i++) {
            ui_led led[2];
            ui_led_pattern(k == 3 ? UI_IMPORTANT : (ui_urgency)k, UI_CAT_DOOR, k == 3, (uint32_t)i * 10, led);
            const bool on0 = led[0].r || led[0].g || led[0].b, on1 = led[1].r || led[1].g || led[1].b;
            if (on0 && !prev) changes++;
            prev = on0;
            if (i % 5 == 0) { row[0][i / 5] = on0 ? '#' : '.'; row[1][i / 5] = on1 ? '#' : '.'; }
        }
        row[0][80] = row[1][80] = 0;
        printf("LED %-9s 0 %s  (%d flashes in 4 s)\n              1 %s\n", names[k], row[0], changes, row[1]);
    }
}

int main(void)
{
    ui_init(canvas);
    fill_spectrogram();

    ui_home home = {
        .listening = true, .mode = "Home", .room_db = -41.0f, .floor_db = -58.0f,
        .top = {{"Doorbell", 94, true}, {"Phone ringing", 31, false}, {"Microwave", 12, false}},
        .recent = events, .n_recent = 3,
    };
    ui_home_draw(&home, NOW);
    save("01_home");

    ui_alert_draw(&events[0], NOW - 2 * MIN + 4000, 0);
    save("02_alert_doorbell");

    ui_alert_draw(&events[1], NOW - 9 * MIN + 2000, 0);
    save("03_alert_knock_felt");

    ui_event smoke = events[5];
    smoke.t_ms = NOW - 3000;
    smoke.count = 4;
    ui_alert_draw(&smoke, NOW, 0);
    save("04_alert_urgent_a");
    ui_alert_draw(&smoke, NOW, 1);
    save("05_alert_urgent_b");

    ui_alert_draw(&events[4], NOW - 70 * MIN + 1000, 0);
    save("06_alert_maybe");

    ui_home_draw(&home, NOW);
    ui_banner_draw(&events[3], NOW - 51 * MIN + 2000);
    save("07_banner_info");

    ui_timeline_draw(events, N_EVENTS, NOW, 0);
    save("08_timeline");

    ui_teach teach = {
        .label = "Our doorbell", .icon = UI_ICON_DOORBELL, .cat = UI_CAT_DOOR, .n_shots = 5,
        .shot = {UI_SHOT_OK, UI_SHOT_OK, UI_SHOT_BAD, UI_SHOT_OK, UI_SHOT_WAITING},
        .shot_db = {24, 21, 4, 26, 0},
        .shot_note = {"ok", "ok", "too quiet", "ok", NULL},
        .level_db = -36.0f, .floor_db = -58.0f, .test_score = -1,
    };
    ui_teach_draw(&teach);
    save("09_teach");

    ui_stats stats = {
        .npu_us = 1240, .runner_us = 1310, .feat_us = 380, .latency_med_ms = 212, .latency_p90_ms = 260,
        .cpu_active_pct = 7.8f, .npu_busy_pct = 2.5f, .infer_per_s = 20, .model_kb = 132,
        .emb_dim = 64, .n_builtin = 7, .n_taught = 3, .save_ms = 96, .uptime_s = 3 * 3600 + 25 * 60,
        .dropped = 0,
    };
    ui_stats_draw(&stats);
    save("10_stats");

    /* extras, not on the contact sheet */
    teach.shot[2] = UI_SHOT_OK;
    teach.shot_db[2] = 22;
    teach.shot_note[2] = "ok";
    teach.shot[4] = UI_SHOT_OK;
    teach.shot_db[4] = 23;
    teach.test_score = 87;
    teach.level_db = -55.0f;
    ui_teach_draw(&teach);
    save("11_teach_done");

    icon_sheet();
    save("12_icons");

    ui_set_clock(11 * 3600000u + 15 * MIN);       /* wall clock: boot was at 11:15 */
    ui_timeline_draw(events, N_EVENTS, NOW, 0);
    save("13_timeline_clock");

    /* boot self-test (S1): all good, then a failure */
    ui_ready ready = {.n = 5, .done = true, .boot_ms = 2140};
    ready.item[0] = (ui_check){"Microphones", "left -52 dB, right -50 dB", UI_CHECK_OK};
    ready.item[1] = (ui_check){"Neural processor", "matches the reference, 1.0 ms", UI_CHECK_OK};
    ready.item[2] = (ui_check){"Saved sounds", "3 taught sounds restored", UI_CHECK_OK};
    ready.item[3] = (ui_check){"Vibration sensor", "knocks can be felt", UI_CHECK_OK};
    ready.item[4] = (ui_check){"Display and lights", "ok", UI_CHECK_OK};
    ui_ready_draw(&ready);
    save("14_ready");
    ready.item[0] = (ui_check){"Microphones", "right mic silent", UI_CHECK_FAIL};
    ready.item[3] = (ui_check){"Vibration sensor", "not found: knocks heard only", UI_CHECK_WARN};
    ui_ready_draw(&ready);
    save("15_not_ready");
    home.top[0] = (ui_guess){"Quiet: NPU resting", 0, false};
    home.top[1] = home.top[2] = (ui_guess){NULL, 0, false};
    ui_home_draw(&home, NOW);
    save("16_home_npu_resting");
    smoke.unanswered_ms = 75000;                  /* A5: urgent, nobody pressed for 75 s */
    ui_alert_draw(&smoke, NOW, 0);
    save("17_alert_escalated_a");
    ui_alert_draw(&smoke, NOW, 1);
    save("18_alert_escalated_b");
    /* settings menu (A6 night mode, T7 forget a taught sound) */
    ui_menu menu = {.title = "Settings", .n = 5, .sel = 0};
    menu.item[0] = "Night mode", menu.value[0] = "Off";
    menu.item[1] = "Forget Keys", menu.value[1] = "taught", menu.danger[1] = true;
    menu.item[2] = "Forget Doorbell", menu.value[2] = "taught", menu.danger[2] = true;
    menu.item[3] = "Forget all 2 taught", menu.danger[3] = true;
    menu.item[4] = "Back";
    menu.note = "Press for night mode";
    ui_menu_draw(&menu);
    save("19_menu");
    menu.sel = 2;
    menu.note = "Press again to forget Doorbell";
    ui_menu_draw(&menu);
    save("20_menu_confirm");
    menu.n = 11;
    for (int i = 1; i < 9; i++) menu.item[i] = "Forget Microwave", menu.value[i] = "taught", menu.danger[i] = true;
    menu.item[9] = "Forget all 8 taught", menu.danger[9] = true, menu.value[9] = NULL;
    menu.item[10] = "Back", menu.danger[10] = false, menu.value[10] = NULL;
    menu.sel = 9;
    menu.note = "Press again to forget all 8";
    ui_menu_draw(&menu);
    save("21_menu_full");
    /* T4: the top row is selected; one sound marked wrong, one right */
    ui_event tl[N_EVENTS];
    memcpy(tl, events, sizeof(tl));
    tl[0].fixed = 2, tl[0].acked = true;
    tl[2].fixed = 1, tl[2].acked = true;
    ui_timeline_draw(tl, N_EVENTS, NOW, 0);
    save("23_timeline_marked");
    /* H6: a loud sound that matches nothing */
    ui_event what = {"What was that?", UI_ICON_UNKNOWN, UI_CAT_OTHER, UI_INFO, false, UI_FELT_NA, 0, 1, NOW - 2000, 0, false, 0, 0};
    ui_home_draw(&home, NOW);
    ui_banner_draw(&what, NOW);
    save("24_what_was_that");
    /* T5: the knob was just turned */
    ui_home_draw(&home, NOW);
    ui_toast_draw("Sensitivity 70%", "More alerts (thresholds -0.04)", 70);
    save("25_knob_sensitivity");
    home.mode = "Night";
    ui_home_draw(&home, NOW);
    save("22_home_night");
    home.mode = "Home";
    home.top[0] = (ui_guess){"Doorbell", 94, true};
    home.top[1] = (ui_guess){"Phone ringing", 31, false};
    home.top[2] = (ui_guess){"Microwave", 12, false};

    gfx_dirty_reset();
    ui_home_update_live(&home);
    int y0, y1;
    gfx_dirty_rows(&y0, &y1);
    printf("live update touches rows %d..%d (%d of %d)\n", y0, y1, y1 - y0 + 1, GFX_H);

    write_png("out/sheet.png", &sheet[0][0][0], SHEET_W, SHEET_H);
    led_trace();
    return 0;
}
