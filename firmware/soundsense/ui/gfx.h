/* Sound Sense drawing primitives for the 480x800 RGB888 panel.
 *
 * Everything draws into one canvas (the back buffer; bytes in B, G, R order like the panel) with
 * anti-aliased edges. The rows touched since gfx_dirty_reset() are tracked, so the caller can
 * copy only those rows to the scanned frame buffer instead of all 1.15 MB.
 *
 * Plain C99 with no board dependencies: tools/ui_sim builds the same files on the host. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GFX_W 480
#define GFX_H 800

typedef struct { uint8_t r, g, b; } gfx_color;

/* 8-bit bitmap font, glyphs first..last, each glyph a w x (h + 2 pad) bitmap, row-major.
 * Fixed-width when widths is NULL (every glyph fixed_w wide, stored back to back).
 * Each byte is coverage (0..255), or for sdf fonts the signed distance to the outline:
 * 128 at the edge, sdf_scale units per stored pixel, larger inside. Distance fonts stay sharp
 * at any size, so one stored size serves every text size on screen. */
typedef struct {
    uint16_t h;               /* line height of the stored glyphs, px, padding excluded */
    uint8_t first, last;
    uint16_t fixed_w;
    const uint8_t *widths;    /* per-glyph bitmap width (proportional fonts) or NULL */
    const uint32_t *offsets;  /* per-glyph byte offset into data (proportional fonts) or NULL */
    const uint8_t *data;
    const uint16_t *adv;      /* per-glyph advance in 1/16 px, or NULL to advance by the width */
    uint8_t pad;              /* blank margin on every side of each bitmap, px */
    uint8_t sdf_scale;        /* 0 = coverage font, else distance units per stored px */
} gfx_font;

void gfx_init(uint8_t (*canvas)[GFX_W][3]);

/* Rows drawn since the last reset; false when nothing was drawn. */
void gfx_dirty_reset(void);
bool gfx_dirty_rows(int *y0, int *y1);

void gfx_fill(int x, int y, int w, int h, gfx_color c);
void gfx_round_rect(int x, int y, int w, int h, int r, gfx_color c);
void gfx_round_frame(int x, int y, int w, int h, int r, int t, gfx_color c);
void gfx_circle(float cx, float cy, float r, gfx_color c);
void gfx_ring(float cx, float cy, float r, float t, gfx_color c);
/* Arc of a ring between angles a0..a1 (radians, 0 = right, counter-clockwise, y up). */
void gfx_arc(float cx, float cy, float r, float t, float a0, float a1, gfx_color c);
void gfx_line(float x0, float y0, float x1, float y1, float t, gfx_color c);
/* Convex polygon of 3..8 points given as x0, y0, x1, y1, ... in either winding. */
void gfx_poly(const float *xy, int n, gfx_color c);
void gfx_triangle(float x0, float y0, float x1, float y1, float x2, float y2, gfx_color c);
void gfx_hbar(int x, int y, int w, int h, float frac, gfx_color fg, gfx_color bg);

/* Text at a pixel height (the font is scaled with bilinear filtering). Returns the end x. */
int gfx_text(const gfx_font *f, int x, int y, const char *s, gfx_color c, int px);
int gfx_text_width(const gfx_font *f, const char *s, int px);
void gfx_text_center(const gfx_font *f, int cx, int y, const char *s, gfx_color c, int px);
/* Largest height <= max_px at which s fits in max_w. */
int gfx_fit_px(const gfx_font *f, const char *s, int max_w, int max_px);

#ifdef __cplusplus
}
#endif
