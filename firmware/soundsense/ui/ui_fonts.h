/* UI fonts: DejaVu Sans Bold and Book as distance fields (ui_fonts.c, made by
 * tools/ui_sim/gen_font.py), sharp at any size. */
#pragma once
#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const gfx_font ui_font_bold;
extern const gfx_font ui_font_book;

/* Extra glyphs beyond ASCII, for use inside string literals. */
#define UI_DOT "\x7f"     /* middle dot, as a separator */
#define UI_TIMES "\x80"   /* multiplication sign, for counts */

#ifdef __cplusplus
}
#endif
