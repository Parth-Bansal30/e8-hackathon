/* Sound Sense screens. See ui.h and docs/SOUND-SENSE.md §6. */
#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui_fonts.h"

/* Bold for labels and values, Book for secondary text. */
#define B (&ui_font_bold)
#define L (&ui_font_book)

/* ---------- theme ---------- */

static const gfx_color BG     = {12, 15, 21};
static const gfx_color PANEL  = {26, 31, 41};
static const gfx_color PANEL2 = {40, 47, 61};
static const gfx_color TEXT   = {242, 244, 247};
static const gfx_color MUTED  = {150, 159, 174};
static const gfx_color FAINT  = {78, 87, 102};
static const gfx_color WHITE  = {255, 255, 255};
static const gfx_color INK    = {12, 15, 21};

/* Okabe-Ito colour-blind-safe palette */
static const gfx_color ORANGE     = {230, 159, 0};
static const gfx_color SKY        = {86, 180, 233};
static const gfx_color GREEN      = {0, 158, 115};
static const gfx_color YELLOW     = {240, 228, 66};
static const gfx_color BLUE       = {0, 114, 178};
static const gfx_color VERMILLION = {213, 94, 0};
static const gfx_color PURPLE     = {204, 121, 167};

gfx_color ui_cat_color(ui_category c)
{
    switch (c) {
    case UI_CAT_SAFETY:    return VERMILLION;
    case UI_CAT_DOOR:      return BLUE;
    case UI_CAT_FAMILY:    return PURPLE;
    case UI_CAT_ANIMAL:    return ORANGE;
    case UI_CAT_APPLIANCE: return SKY;
    case UI_CAT_WATER:     return GREEN;
    case UI_CAT_VOICE:     return MUTED;
    default:               return YELLOW;
    }
}

static gfx_color mix(gfx_color a, gfx_color b, float t)
{
    return (gfx_color){(uint8_t)(a.r + (b.r - a.r) * t), (uint8_t)(a.g + (b.g - a.g) * t),
                       (uint8_t)(a.b + (b.b - a.b) * t)};
}

/* Dark or white text, whichever reads better on bg. */
static gfx_color ink_on(gfx_color bg)
{
    const float lum = (0.2126f * bg.r + 0.7152f * bg.g + 0.0722f * bg.b) / 255.0f;
    return lum > 0.45f ? INK : WHITE;
}

/* ---------- time ---------- */

static uint32_t clock_base;
static bool clock_set;

void ui_set_clock(uint32_t ms_after_midnight_at_boot)
{
    clock_base = ms_after_midnight_at_boot;
    clock_set = true;
}

static void fmt_ago(char *b, size_t n, uint32_t t_ms, uint32_t now_ms)
{
    const uint32_t s = now_ms > t_ms ? (now_ms - t_ms) / 1000 : 0;
    if (s < 60) snprintf(b, n, "just now");
    else if (s < 3600) snprintf(b, n, "%u min ago", (unsigned)(s / 60));
    else snprintf(b, n, "%u h %u min ago", (unsigned)(s / 3600), (unsigned)(s / 60 % 60));
}

/* Clock time when set ("14:02"), otherwise relative ("12 min ago"). */
static void fmt_when(char *b, size_t n, uint32_t t_ms, uint32_t now_ms)
{
    if (!clock_set) { fmt_ago(b, n, t_ms, now_ms); return; }
    const uint32_t m = (clock_base + t_ms) / 60000 % 1440;
    snprintf(b, n, "%02u:%02u", (unsigned)(m / 60), (unsigned)(m % 60));
}

static void fmt_dur(char *b, size_t n, uint32_t ms)
{
    if (ms < 60000) snprintf(b, n, "%u s", (unsigned)(ms / 1000));
    else snprintf(b, n, "%u min", (unsigned)(ms / 60000));
}

static const char *felt_text(ui_felt f)
{
    return f == UI_FELT_YES ? "Felt here + heard" : f == UI_FELT_HEARD_ONLY ? "Heard only (TV?)" : NULL;
}

#define SEP "  " UI_DOT "  "

/* "2 min ago  ·  ×3  ·  felt here" style detail line for lists. */
static void fmt_detail(char *b, size_t n, const ui_event *e, uint32_t now_ms, bool with_when)
{
    char part[40];
    b[0] = 0;
    if (with_when) { fmt_ago(part, sizeof part, e->t_ms, now_ms); strncat(b, part, n - strlen(b) - 1); }
    if (e->count > 1) {
        snprintf(part, sizeof part, "%s" UI_TIMES "%u", b[0] ? SEP : "", (unsigned)e->count);
        strncat(b, part, n - strlen(b) - 1);
    }
    if (e->dur_ms >= 10000) {
        char d[16];
        fmt_dur(d, sizeof d, e->dur_ms);
        snprintf(part, sizeof part, "%sfor %s", b[0] ? SEP : "", d);
        strncat(b, part, n - strlen(b) - 1);
    }
    if (e->felt == UI_FELT_YES) strncat(b, b[0] ? SEP "felt here" : "felt here", n - strlen(b) - 1);
    if (e->felt == UI_FELT_HEARD_ONLY) strncat(b, b[0] ? SEP "heard only" : "heard only", n - strlen(b) - 1);
}

/* ---------- icons (drawn on a 100 x 100 grid centred on 0, 0) ---------- */

void ui_icon_draw(ui_icon icon, float cx, float cy, float size, gfx_color fg, gfx_color bg)
{
    const float u = size / 100.0f;
#define X(v) (cx + (v) * u)
#define Y(v) (cy + (v) * u)
#define R(v) ((v) * u)
#define LINE(a, b, c, d, t) gfx_line(X(a), Y(b), X(c), Y(d), R(t), fg)
#define DISC(a, b, r) gfx_circle(X(a), Y(b), R(r), fg)
    switch (icon) {
    case UI_ICON_KNOCK:                   /* door with knock waves */
        gfx_round_frame((int)X(-34), (int)Y(-42), (int)R(50), (int)R(84), (int)R(5), (int)R(7), fg);
        DISC(4, 2, 5);
        gfx_arc(X(14), Y(0), R(24), R(6), -0.75f, 0.75f, fg);
        gfx_arc(X(14), Y(0), R(36), R(6), -0.65f, 0.65f, fg);
        break;
    case UI_ICON_DOORBELL:
    case UI_ICON_ALARM: {                 /* bell; the doorbell adds ring waves */
        DISC(0, -10, 24);
        const float body[8] = {X(-24), Y(-10), X(24), Y(-10), X(30), Y(22), X(-30), Y(22)};
        gfx_poly(body, 4, fg);
        gfx_round_rect((int)X(-36), (int)Y(18), (int)R(72), (int)R(10), (int)R(5), fg);
        DISC(0, 34, 7);
        DISC(0, -36, 5);
        if (icon == UI_ICON_DOORBELL) {
            gfx_arc(X(0), Y(-6), R(46), R(6), 0.25f, 0.85f, fg);
            gfx_arc(X(0), Y(-6), R(46), R(6), 2.29f, 2.89f, fg);
        } else {                          /* alarm: "!" waves on both sides */
            LINE(-44, -30, -36, -22, 6);
            LINE(44, -30, 36, -22, 6);
            LINE(-46, -6, -38, -6, 6);
            LINE(46, -6, 38, -6, 6);
        }
        break;
    }
    case UI_ICON_SMOKE:                   /* flame: leaning main tongue plus two side licks */
        DISC(0, 18, 26);
        gfx_triangle(X(-25), Y(12), X(25), Y(12), X(6), Y(-48), fg);
        gfx_triangle(X(-26), Y(16), X(-8), Y(0), X(-30), Y(-22), fg);
        gfx_triangle(X(26), Y(16), X(10), Y(-2), X(30), Y(-16), fg);
        gfx_circle(X(0), Y(28), R(11), bg);
        gfx_triangle(X(-10), Y(25), X(10), Y(25), X(3), Y(2), bg);
        break;
    case UI_ICON_CO:
        gfx_ring(X(0), Y(0), R(42), R(7), fg);
        gfx_text_center(B, (int)X(0), (int)Y(-17), "CO", fg, (int)R(36));
        break;
    case UI_ICON_GLASS:                   /* wine glass with a crack */
        gfx_triangle(X(-28), Y(-42), X(28), Y(-42), X(0), Y(6), fg);
        LINE(0, 4, 0, 34, 7);
        LINE(-18, 36, 18, 36, 7);
        gfx_line(X(-14), Y(-38), X(-4), Y(-28), R(4), bg);
        gfx_line(X(-4), Y(-28), X(-11), Y(-19), R(4), bg);
        gfx_line(X(-11), Y(-19), X(0), Y(-10), R(4), bg);
        break;
    case UI_ICON_SIREN: {                 /* dome light with rays */
        DISC(0, 4, 22);
        gfx_fill((int)X(-22), (int)Y(4), (int)R(44), (int)R(20), fg);
        gfx_round_rect((int)X(-32), (int)Y(22), (int)R(64), (int)R(12), (int)R(4), fg);
        static const float ang[5] = {0.45f, 1.0f, 1.5708f, 2.14f, 2.69f};
        for (int i = 0; i < 5; i++) {
            const float ca = cosf(ang[i]), sa = -sinf(ang[i]);
            LINE(32 * ca, 4 + 32 * sa, 44 * ca, 4 + 44 * sa, 6);
        }
        break;
    }
    case UI_ICON_WATER:                   /* drop */
        DISC(0, 14, 27);
        gfx_triangle(X(-25), Y(4), X(25), Y(4), X(0), Y(-44), fg);
        gfx_arc(X(0), Y(14), R(17), R(4), 3.4f, 4.4f, bg);
        break;
    case UI_ICON_SPEECH:                  /* speech bubble */
        gfx_round_rect((int)X(-40), (int)Y(-34), (int)R(80), (int)R(56), (int)R(16), fg);
        gfx_triangle(X(-20), Y(18), X(2), Y(18), X(-26), Y(40), fg);
        gfx_circle(X(-17), Y(-6), R(6), bg);
        gfx_circle(X(0), Y(-6), R(6), bg);
        gfx_circle(X(17), Y(-6), R(6), bg);
        break;
    case UI_ICON_NAME:                    /* person, with voice waves */
        DISC(-12, -18, 15);
        gfx_round_rect((int)X(-40), (int)Y(4), (int)R(56), (int)R(46), (int)R(22), fg);
        gfx_fill((int)X(-44), (int)Y(36), (int)R(64), (int)R(16), bg);
        gfx_arc(X(12), Y(-18), R(16), R(5), -0.75f, 0.75f, fg);
        gfx_arc(X(12), Y(-18), R(28), R(5), -0.65f, 0.65f, fg);
        break;
    case UI_ICON_MICROWAVE:
        gfx_round_frame((int)X(-44), (int)Y(-32), (int)R(88), (int)R(64), (int)R(7), (int)R(6), fg);
        gfx_round_rect((int)X(-34), (int)Y(-22), (int)R(50), (int)R(44), (int)R(5), fg);
        DISC(28, -10, 5);
        DISC(28, 8, 5);
        break;
    case UI_ICON_KETTLE:
        gfx_round_rect((int)X(-28), (int)Y(-20), (int)R(54), (int)R(52), (int)R(14), fg);
        LINE(-16, -26, 14, -26, 6);
        DISC(-1, -33, 5);
        LINE(22, -4, 40, -22, 8);
        gfx_arc(X(-28), Y(6), R(18), R(6), 1.5708f, 4.7124f, fg);
        break;
    case UI_ICON_WASHER:
        gfx_round_frame((int)X(-38), (int)Y(-42), (int)R(76), (int)R(84), (int)R(9), (int)R(6), fg);
        LINE(-33, -24, 33, -24, 4);
        DISC(-24, -33, 3.5f);
        DISC(-13, -33, 3.5f);
        gfx_ring(X(0), Y(9), R(21), R(6), fg);
        break;
    case UI_ICON_PHONE:
        gfx_round_frame((int)X(-21), (int)Y(-42), (int)R(42), (int)R(84), (int)R(9), (int)R(6), fg);
        LINE(-6, -31, 6, -31, 4);
        DISC(0, 31, 4.5f);
        LINE(-32, -12, -32, 12, 5);
        LINE(32, -12, 32, 12, 5);
        LINE(-42, -6, -42, 6, 5);
        LINE(42, -6, 42, 6, 5);
        break;
    case UI_ICON_DOG:                     /* paw print */
        gfx_round_rect((int)X(-22), (int)Y(-2), (int)R(44), (int)R(40), (int)R(18), fg);
        DISC(-30, -12, 10);
        DISC(-12, -32, 11);
        DISC(12, -32, 11);
        DISC(30, -12, 10);
        break;
    case UI_ICON_BABY:                    /* baby face */
        DISC(0, 6, 34);
        gfx_arc(X(6), Y(-36), R(9), R(5), -0.3f, 3.3f, fg);
        gfx_circle(X(-12), Y(2), R(4.5f), bg);
        gfx_circle(X(12), Y(2), R(4.5f), bg);
        gfx_arc(X(0), Y(12), R(11), R(4), 3.7f, 5.7f, bg);
        break;
    case UI_ICON_UNKNOWN:
        gfx_ring(X(0), Y(0), R(42), R(7), fg);
        gfx_text_center(B, (int)X(0), (int)Y(-27), "?", fg, (int)R(56));
        break;
    case UI_ICON_CUSTOM:                  /* two music notes */
    default:
        DISC(-20, 26, 12);
        DISC(20, 18, 12);
        LINE(-10, 26, -10, -26, 6);
        LINE(30, 18, 30, -34, 6);
        LINE(-10, -28, 30, -36, 9);
        break;
    }
#undef X
#undef Y
#undef R
#undef LINE
#undef DISC
}

/* Category-coloured disc with the icon on it. */
static void badge(ui_icon icon, ui_category c, float cx, float cy, float r)
{
    const gfx_color bg = ui_cat_color(c);
    gfx_circle(cx, cy, r, bg);
    ui_icon_draw(icon, cx, cy, r * 1.25f, ink_on(bg), bg);
}

/* ---------- shared layout ---------- */

static void header(const char *title, const char *right, gfx_color right_col)
{
    gfx_fill(0, 0, GFX_W, 80, BG);
    gfx_text(B, 24, 22, title, TEXT, 34);
    if (right) {
        const int w = gfx_text_width(B, right, 20) + 44;
        gfx_round_rect(GFX_W - 24 - w, 22, w, 36, 18, PANEL2);
        gfx_circle((float)(GFX_W - 24 - w + 20), 40.0f, 6.0f, right_col);
        gfx_text(B, GFX_W - 24 - w + 34, 29, right, TEXT, 20);
    }
}

static void footer(const char *hint)
{
    gfx_fill(0, GFX_H - 48, GFX_W, 48, BG);
    gfx_text_center(L, GFX_W / 2, GFX_H - 36, hint, FAINT, 18);
}

/* ---------- spectrogram ---------- */

#define SPEC_X 32
#define SPEC_Y 140
#define SPEC_COLS 416
#define SPEC_BAND_PX 3
static uint8_t spec[SPEC_COLS][40];
static int spec_head;
static gfx_color lut[256];

static void make_lut(void)
{
    /* viridis-like: perceptually even and colour-blind safe */
    static const gfx_color k[5] = {{20, 18, 40}, {59, 82, 139}, {33, 145, 140}, {94, 201, 98}, {253, 231, 37}};
    for (int i = 0; i < 256; i++) {
        const float t = i / 255.0f * 4.0f;
        const int s = t >= 4.0f ? 3 : (int)t;
        lut[i] = mix(k[s], k[s + 1], t - s);
    }
}

void ui_spectro_push(const uint8_t col[40])
{
    memcpy(spec[spec_head], col, 40);
    spec_head = (spec_head + 1) % SPEC_COLS;
}

static void draw_spectro(void)
{
    for (int c = 0; c < SPEC_COLS; c++) {
        const uint8_t *col = spec[(spec_head + c) % SPEC_COLS];   /* oldest on the left */
        for (int b = 0; b < 40; b++)
            gfx_fill(SPEC_X + c, SPEC_Y + (39 - b) * SPEC_BAND_PX, 1, SPEC_BAND_PX, lut[col[b]]);
    }
}

/* ---------- home ---------- */

#define LIVE_Y 96
#define TOP_Y (SPEC_Y + 40 * SPEC_BAND_PX + 12)
#define LEVEL_Y (TOP_Y + 96 + 16)
#define RECENT_Y (LEVEL_Y + 76 + 16)

static void draw_top(const ui_home *h)
{
    for (int i = 0; i < 3; i++) {
        const int y = TOP_Y + i * 30;
        gfx_fill(SPEC_X, y, SPEC_COLS, 30, PANEL);
        const ui_guess *g = &h->top[i];
        if (!g->label) continue;
        char pct[8];
        snprintf(pct, sizeof pct, "%u%%", (unsigned)g->score);
        const gfx_color c = g->over_threshold ? GREEN : FAINT;
        gfx_text(i == 0 ? B : L, SPEC_X, y + 3, g->label, i == 0 ? TEXT : MUTED, 22);
        gfx_hbar(SPEC_X + 170, y + 9, 176, 12, g->score / 100.0f, c, PANEL2);
        gfx_text(B, SPEC_X + 360, y + 3, pct, i == 0 ? TEXT : MUTED, 22);
    }
}

static void draw_level(const ui_home *h)
{
    gfx_round_rect(16, LEVEL_Y, 448, 76, 18, PANEL);
    char db[24];
    snprintf(db, sizeof db, "%d dBFS", (int)lrintf(h->room_db));
    const bool loud = h->room_db > h->floor_db + 10.0f;
    gfx_text(L, 32, LEVEL_Y + 12, "Room", MUTED, 20);
    gfx_text(B, 32, LEVEL_Y + 38, loud ? "sound" : "quiet", TEXT, 26);
    const float lo = -80.0f, hi = 0.0f;
    const int bx = 150, bw = 200;
    gfx_hbar(bx, LEVEL_Y + 32, bw, 14, (h->room_db - lo) / (hi - lo), loud ? SKY : GREEN, PANEL2);
    const int fx = bx + (int)(bw * (h->floor_db - lo) / (hi - lo));
    gfx_fill(fx - 1, LEVEL_Y + 24, 3, 30, MUTED);              /* noise floor marker */
    gfx_text(L, 362, LEVEL_Y + 28, db, MUTED, 20);
}

static void draw_recent(const ui_home *h, uint32_t now_ms)
{
    gfx_fill(0, RECENT_Y, GFX_W, GFX_H - 48 - RECENT_Y, BG);
    gfx_text(L, 24, RECENT_Y, "Recent", MUTED, 20);
    if (h->n_recent == 0) {
        gfx_text(L, 24, RECENT_Y + 40, "Nothing yet. All quiet.", FAINT, 24);
        return;
    }
    for (int i = 0; i < h->n_recent && i < 3; i++) {
        const ui_event *e = &h->recent[i];
        const int y = RECENT_Y + 32 + i * 76;
        char label[48], detail[64];
        gfx_round_rect(16, y, 448, 68, 16, PANEL);
        badge(e->icon, e->cat, 52, y + 34, 24);
        snprintf(label, sizeof label, e->maybe ? "Maybe: %s?" : "%s", e->label);
        gfx_text(B, 90, y + 8, label, e->maybe ? MUTED : TEXT, 26);
        fmt_detail(detail, sizeof detail, e, now_ms, true);
        gfx_text(L, 90, y + 40, detail, MUTED, 18);
        if (!e->acked) gfx_circle(444, (float)(y + 34), 7, ui_cat_color(e->cat));
    }
}

void ui_home_draw(const ui_home *h, uint32_t now_ms)
{
    gfx_fill(0, 0, GFX_W, GFX_H, BG);
    header("Sound Sense", h->listening ? "Listening" : "Mic off", h->listening ? GREEN : VERMILLION);
    gfx_round_rect(16, LIVE_Y, 448, TOP_Y + 96 - LIVE_Y, 18, PANEL);
    char mode[32];
    snprintf(mode, sizeof mode, "Now hearing  " UI_DOT "  %s mode", h->mode ? h->mode : "Home");
    gfx_text(L, 32, LIVE_Y + 14, mode, MUTED, 20);
    draw_spectro();
    draw_top(h);
    draw_level(h);
    draw_recent(h, now_ms);
    footer("UP teach   LEFT timeline   RIGHT stats");
}

void ui_home_update_live(const ui_home *h)
{
    draw_spectro();
    draw_top(h);
    draw_level(h);
}

/* ---------- alerts ---------- */

void ui_alert_draw(const ui_event *e, uint32_t now_ms, int phase)
{
    gfx_color bg, ink;
    if (e->urgency == UI_URGENT && !e->maybe) {
        bg = phase ? WHITE : VERMILLION;
        ink = phase ? VERMILLION : WHITE;
    } else if (e->maybe) {
        bg = PANEL2;
        ink = TEXT;
    } else {
        bg = ui_cat_color(e->cat);
        ink = ink_on(bg);
    }
    gfx_fill(0, 0, GFX_W, GFX_H, bg);
    if (e->maybe) gfx_round_frame(10, 10, GFX_W - 20, GFX_H - 20, 28, 6, ui_cat_color(e->cat));

    char when[24], line[64], label[48];
    fmt_when(when, sizeof when, e->t_ms, now_ms);
    if (e->urgency == UI_URGENT && !e->maybe) gfx_text_center(B, GFX_W / 2, 48, "URGENT", ink, 40);
    else gfx_text_center(L, GFX_W / 2, 52, when, mix(bg, ink, 0.8f), 28);

    const float cy = 290.0f;
    if (e->maybe) {
        gfx_circle(GFX_W / 2.0f, cy, 132, PANEL);
        ui_icon_draw(e->icon, GFX_W / 2.0f, cy, 170, ui_cat_color(e->cat), PANEL);
    } else {
        gfx_circle(GFX_W / 2.0f, cy, 132, mix(bg, ink, 0.16f));
        ui_icon_draw(e->icon, GFX_W / 2.0f, cy, 170, ink, mix(bg, ink, 0.16f));
    }

    snprintf(label, sizeof label, e->maybe ? "Maybe: %s?" : "%s", e->label);
    if (e->urgency == UI_URGENT && !e->maybe)
        for (char *p = label; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
    const int px = gfx_fit_px(B, label, 440, 88);
    gfx_text_center(B, GFX_W / 2, 460, label, ink, px);

    if (e->urgency == UI_URGENT && !e->maybe) snprintf(line, sizeof line, "%s  " UI_DOT "  %u%% sure", when, e->confidence);
    else snprintf(line, sizeof line, "%u%% sure", e->confidence);
    gfx_text_center(L, GFX_W / 2, 470 + px, line, mix(bg, ink, 0.85f), 30);

    int y = 530 + px;
    const char *felt = felt_text(e->felt);
    if (felt) {
        const int w = gfx_text_width(B, felt, 26) + 48;
        gfx_round_rect(GFX_W / 2 - w / 2, y, w, 48, 24, mix(bg, ink, 0.18f));
        gfx_text_center(B, GFX_W / 2, y + 10, felt, ink, 26);
        y += 66;
    }
    if (e->count > 1 || e->dur_ms >= 10000) {
        char d[16];
        fmt_dur(d, sizeof d, e->dur_ms);
        if (e->dur_ms >= 10000) snprintf(line, sizeof line, "%u times, for %s", (unsigned)e->count, d);
        else snprintf(line, sizeof line, "%u times", (unsigned)e->count);
        gfx_text_center(L, GFX_W / 2, y, line, ink, 28);
    }
    gfx_text_center(L, GFX_W / 2, GFX_H - 70,
                    e->maybe ? "Tap: right / wrong" : "Tap anywhere to dismiss", mix(bg, ink, 0.7f), 24);
}

void ui_banner_draw(const ui_event *e, uint32_t now_ms)
{
    const gfx_color bg = ui_cat_color(e->cat), ink = ink_on(bg);
    gfx_fill(0, 0, GFX_W, 132, BG);
    gfx_round_rect(12, 12, GFX_W - 24, 112, 22, bg);
    gfx_circle(70, 68, 40, mix(bg, ink, 0.16f));
    ui_icon_draw(e->icon, 70, 68, 52, ink, mix(bg, ink, 0.16f));
    char label[48], detail[64];
    snprintf(label, sizeof label, e->maybe ? "Maybe: %s?" : "%s", e->label);
    gfx_text(B, 128, 30, label, ink, gfx_fit_px(B, label, 320, 40));
    fmt_detail(detail, sizeof detail, e, now_ms, true);
    gfx_text(L, 128, 80, detail, mix(bg, ink, 0.8f), 20);
}

/* ---------- timeline ---------- */

void ui_timeline_draw(const ui_event *ev, int n, uint32_t now_ms, int first)
{
    gfx_fill(0, 0, GFX_W, GFX_H, BG);
    char count[24];
    snprintf(count, sizeof count, "%d sound%s", n, n == 1 ? "" : "s");
    header("Today", count, MUTED);
    for (int i = first, row = 0; i < n && row < 8; i++, row++) {
        const ui_event *e = &ev[i];
        const int y = 92 + row * 82;
        char when[24], label[48], detail[64];
        gfx_round_rect(16, y, 448, 74, 16, PANEL);
        gfx_round_rect(16, y, 8, 74, 4, e->urgency == UI_URGENT ? VERMILLION
                                         : e->urgency == UI_IMPORTANT ? TEXT : FAINT);
        fmt_when(when, sizeof when, e->t_ms, now_ms);
        if (!clock_set) {                  /* compact relative time for the column */
            const uint32_t s = (now_ms - e->t_ms) / 1000;
            if (s < 60) snprintf(when, sizeof when, "now");
            else if (s < 3600) snprintf(when, sizeof when, "%um", (unsigned)(s / 60));
            else snprintf(when, sizeof when, "%uh%02um", (unsigned)(s / 3600), (unsigned)(s / 60 % 60));
        }
        gfx_text(B, 36, y + 24, when, MUTED, 22);
        badge(e->icon, e->cat, 150, y + 37, 24);
        snprintf(label, sizeof label, e->maybe ? "Maybe: %s?" : "%s", e->label);
        gfx_text(B, 186, y + 10, label, e->maybe ? MUTED : TEXT, 24);
        fmt_detail(detail, sizeof detail, e, now_ms, false);
        if (!detail[0]) snprintf(detail, sizeof detail, "%u%% sure", e->confidence);
        gfx_text(L, 186, y + 42, detail, MUTED, 18);
        if (!e->acked) gfx_circle(444, (float)(y + 37), 7, ui_cat_color(e->cat));
    }
    footer("Tap a sound: right / wrong / teach");
}

/* ---------- teaching ---------- */

void ui_teach_draw(const ui_teach *t)
{
    gfx_fill(0, 0, GFX_W, GFX_H, BG);
    header("Teach a sound", NULL, BG);

    badge(t->icon, t->cat, GFX_W / 2.0f, 176, 64);
    gfx_text_center(B, GFX_W / 2, 256, t->label, TEXT, 44);

    int done = 0;
    for (int i = 0; i < t->n_shots; i++) done += t->shot[i] == UI_SHOT_OK;
    char prompt[48];
    if (done < t->n_shots) snprintf(prompt, sizeof prompt, "Make the sound now  (%d of %d)", done + 1, t->n_shots);
    else snprintf(prompt, sizeof prompt, "Got all %d. Test it once more.", t->n_shots);
    gfx_text_center(L, GFX_W / 2, 320, prompt, MUTED, 24);

    const int step = 84, x0 = GFX_W / 2 - (t->n_shots - 1) * step / 2;
    for (int i = 0; i < t->n_shots; i++) {
        const float cx = (float)(x0 + i * step), cy = 408.0f;
        char num[4], note[16];
        switch (t->shot[i]) {
        case UI_SHOT_OK:
            gfx_circle(cx, cy, 32, GREEN);
            gfx_line(cx - 14, cy + 1, cx - 4, cy + 11, 6, INK);
            gfx_line(cx - 4, cy + 11, cx + 15, cy - 11, 6, INK);
            snprintf(note, sizeof note, "+%d dB", t->shot_db[i]);
            break;
        case UI_SHOT_BAD:
            gfx_circle(cx, cy, 32, VERMILLION);
            gfx_line(cx - 11, cy - 11, cx + 11, cy + 11, 6, WHITE);
            gfx_line(cx - 11, cy + 11, cx + 11, cy - 11, 6, WHITE);
            snprintf(note, sizeof note, "%s", t->shot_note[i] ? t->shot_note[i] : "retry");
            break;
        default:
            gfx_ring(cx, cy, 32, 4, FAINT);
            snprintf(num, sizeof num, "%d", i + 1);
            gfx_text_center(B, (int)cx, (int)cy - 14, num, FAINT, 28);
            note[0] = 0;
            break;
        }
        if (note[0]) gfx_text_center(L, (int)cx, (int)cy + 42, note,
                                     t->shot[i] == UI_SHOT_BAD ? VERMILLION : MUTED, 16);
    }

    /* live level with the capture trigger (noise floor + 10 dB) */
    gfx_round_rect(16, 486, 448, 96, 18, PANEL);
    gfx_text(L, 32, 498, "Level", MUTED, 20);
    const float lo = -80.0f, hi = 0.0f;
    const int bx = 32, bw = 416, by = 538;
    const bool hot = t->level_db > t->floor_db + 10.0f;
    gfx_hbar(bx, by, bw, 18, (t->level_db - lo) / (hi - lo), hot ? GREEN : SKY, PANEL2);
    const int tx = bx + (int)(bw * (t->floor_db + 10.0f - lo) / (hi - lo));
    gfx_fill(tx - 1, by - 8, 3, 34, TEXT);
    gfx_text(L, tx + 8, 500, "capture", MUTED, 16);

    if (t->test_score >= 0) {
        char s[32];
        snprintf(s, sizeof s, "Test match: %d%%", t->test_score);
        gfx_text_center(B, GFX_W / 2, 606, s, t->test_score >= 70 ? GREEN : ORANGE, 30);
    }

    static const char *btn[3] = {"Retry", "Save", "Cancel"};
    static const char *key[3] = {"LEFT", "PRESS", "RIGHT"};
    for (int i = 0; i < 3; i++) {
        const int x = 16 + i * 152, y = 664;
        const bool primary = i == 1;
        gfx_round_rect(x, y, 144, 72, 18, primary ? GREEN : PANEL2);
        gfx_text_center(B, x + 72, y + 12, btn[i], primary ? INK : TEXT, 28);
        gfx_text_center(L, x + 72, y + 46, key[i], primary ? INK : MUTED, 14);
    }
    gfx_fill(0, GFX_H - 48, GFX_W, 48, BG);
}

/* ---------- stats ---------- */

/* v / div with `dec` decimals, rounded, using integers only: the board's newlib-nano printf has
 * no %f unless linked with -u _printf_float. */
static void fmt_fixed(char *b, size_t n, uint32_t v, uint32_t div, int dec)
{
    const uint32_t p = dec == 2 ? 100 : 10;
    const uint32_t q = (v * p + div / 2) / div;
    snprintf(b, n, dec == 2 ? "%u.%02u" : "%u.%u", (unsigned)(q / p), (unsigned)(q % p));
}

static void tile(int col, int row, const char *value, const char *unit, const char *label)
{
    const int x = 16 + col * 232, y = 92 + row * 130;
    gfx_round_rect(x, y, 216, 118, 18, PANEL);
    const int end = gfx_text(B, x + 18, y + 16, value, TEXT, 48);
    if (unit) gfx_text(L, end + 6, y + 36, unit, MUTED, 22);
    gfx_text(L, x + 18, y + 80, label, MUTED, 18);
}

void ui_stats_draw(const ui_stats *s)
{
    char up[32], v[10][16];
    snprintf(up, sizeof up, "up %u:%02u", (unsigned)(s->uptime_s / 3600), (unsigned)(s->uptime_s / 60 % 60));
    gfx_fill(0, 0, GFX_W, GFX_H, BG);
    header("Under the hood", up, GREEN);
    fmt_fixed(v[0], sizeof v[0], s->npu_us, 1000, 2);
    snprintf(v[1], sizeof v[1], "%u", (unsigned)s->latency_med_ms);
    fmt_fixed(v[2], sizeof v[2], (uint32_t)(s->cpu_active_pct * 10.0f + 0.5f), 10, 1);
    fmt_fixed(v[3], sizeof v[3], (uint32_t)(s->npu_busy_pct * 10.0f + 0.5f), 10, 1);
    snprintf(v[4], sizeof v[4], "%u", (unsigned)(s->infer_per_s + 0.5f));
    snprintf(v[5], sizeof v[5], "%u", (unsigned)s->model_kb);
    snprintf(v[6], sizeof v[6], "%d+%d", s->n_builtin, s->n_taught);
    snprintf(v[7], sizeof v[7], "%u", (unsigned)s->save_ms);
    fmt_fixed(v[8], sizeof v[8], s->feat_us, 1000, 2);
    snprintf(v[9], sizeof v[9], "%u", (unsigned)s->dropped);
    char p90[32];
    snprintf(p90, sizeof p90, "alert delay, p90 %u", (unsigned)s->latency_p90_ms);
    tile(0, 0, v[0], "ms", "NPU per check");
    tile(1, 0, v[1], "ms", p90);
    tile(0, 1, v[2], "%", "CPU busy");
    tile(1, 1, v[3], "%", "NPU busy");
    tile(0, 2, v[4], "/s", "sound checks");
    tile(1, 2, v[5], "KB", "model in MRAM");
    tile(0, 3, v[6], NULL, "built-in + taught");
    tile(1, 3, v[7], "ms", "save to flash");
    tile(0, 4, v[8], "ms", "features per frame");
    tile(1, 4, v[9], NULL, "audio dropped");
    footer("All of this runs on the board. Nothing is recorded.");
}

/* ---------- init ---------- */

void ui_init(uint8_t (*canvas)[GFX_W][3])
{
    gfx_init(canvas);
    make_lut();
}
