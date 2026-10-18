/* Sound Sense drawing: a 480x800 RGB888 back buffer (SRAM1) copied to the scanned frame buffer
 * (SRAM0) by ss_present(). Bytes are B, G, R. Plain C. */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SS_LCD_W 480
#define SS_LCD_H 800

typedef struct { uint8_t r, g, b; } ss_rgb;
enum { SS_FONT_SMALL = 0, SS_FONT_LARGE = 1 };   /* small: ASCII 14x29; large: digits 78x157 */

/* Start the CDC200 and MIPI DSI panel showing the frame buffer. Needs ss_board_init() first. */
void ss_gfx_init(void);

void ss_fill(int x, int y, int w, int h, ss_rgb c);
/* Text with nearest-neighbour scaling; returns the x after the last glyph. */
int ss_text(int font, int x, int y, const char *s, ss_rgb c, int scale);
int ss_text_width(int font, const char *s, int scale);
int ss_font_height(int font);
/* Horizontal bar filled to frac (0..1) over a background. */
void ss_hbar(int x, int y, int w, int h, float frac, ss_rgb fg, ss_rgb bg);

/* Copy the back buffer to the frame buffer and clean the D-cache so the CDC200 sees it.
 * Returns the cycles it took (it moves 1.15 MB). */
uint32_t ss_present(void);

/* The back buffer itself, for drawing code of its own (ui/gfx.c), and a present of rows y0..y1
 * (inclusive) only. Returns cycles. */
uint8_t (*ss_gfx_back(void))[SS_LCD_W][3];
uint32_t ss_present_rows(int y0, int y1);

#ifdef __cplusplus
}
#endif
