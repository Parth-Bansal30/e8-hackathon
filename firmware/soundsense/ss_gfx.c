/* Sound Sense drawing (see ss_gfx.h). */
#include "ss_gfx.h"

#include <string.h>

#include "Driver_CDC200.h"
#include "RTE_Device.h"
#include "assets/fonts.h"   /* ../npu_display/assets */
#include "ss_board.h"

#if RTE_CDC200_PIXEL_FORMAT != 1
#error "drawing code assumes RGB888 (RTE_CDC200_PIXEL_FORMAT 1)"
#endif

static uint8_t fb[SS_LCD_H][SS_LCD_W][3] __attribute__((section(".bss.lcd_frame_buf"), aligned(16)));
static uint8_t back[SS_LCD_H][SS_LCD_W][3] __attribute__((section(".bss.at_sram1"), aligned(16)));

extern ARM_DRIVER_CDC200 Driver_CDC200;

static const font_t *font_of(int f) { return f == SS_FONT_LARGE ? &font_large : &font_small; }

static void display_cb(uint32_t event)
{
    if (event & ARM_CDC_DSI_ERROR_EVENT) ss_halt("DSI error");
}

void ss_gfx_init(void)
{
    ARM_DRIVER_CDC200 *cdc = &Driver_CDC200;
    ss_fill(0, 0, SS_LCD_W, SS_LCD_H, (ss_rgb){16, 20, 32});
    ss_present();
    if (cdc->Initialize(display_cb) != ARM_DRIVER_OK || cdc->PowerControl(ARM_POWER_FULL) != ARM_DRIVER_OK ||
        cdc->Control(CDC200_CONFIGURE_DISPLAY, (uint32_t)fb) != ARM_DRIVER_OK || cdc->Start() != ARM_DRIVER_OK) {
        ss_halt("display start");
    }
}

void ss_fill(int x, int y, int w, int h, ss_rgb c)
{
    const int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    const int x1 = x + w > SS_LCD_W ? SS_LCD_W : x + w, y1 = y + h > SS_LCD_H ? SS_LCD_H : y + h;
    if (x1 <= x0) return;
    for (int j = y0; j < y1; j++) {
        uint8_t *p = back[j][x0];
        for (int i = x0; i < x1; i++, p += 3) {
            p[0] = c.b;
            p[1] = c.g;
            p[2] = c.r;
        }
    }
}

int ss_text(int font, int x, int y, const char *s, ss_rgb c, int scale)
{
    const font_t *f = font_of(font);
    for (; *s; s++, x += f->w * scale) {
        if (*s < f->first || *s > f->last) continue;
        const uint8_t *g = f->data + (size_t)(*s - f->first) * f->w * f->h;
        for (int j = 0; j < f->h * scale; j++) {
            const int py = y + j;
            if (py < 0 || py >= SS_LCD_H) continue;
            for (int i = 0; i < f->w * scale; i++) {
                const int px = x + i;
                const unsigned a = g[(j / scale) * f->w + i / scale];
                if (!a || px < 0 || px >= SS_LCD_W) continue;
                uint8_t *d = back[py][px];
                d[0] = (uint8_t)(d[0] + ((int)c.b - d[0]) * (int)a / 255);
                d[1] = (uint8_t)(d[1] + ((int)c.g - d[1]) * (int)a / 255);
                d[2] = (uint8_t)(d[2] + ((int)c.r - d[2]) * (int)a / 255);
            }
        }
    }
    return x;
}

int ss_text_width(int font, const char *s, int scale) { return (int)strlen(s) * font_of(font)->w * scale; }
int ss_font_height(int font) { return font_of(font)->h; }

void ss_hbar(int x, int y, int w, int h, float frac, ss_rgb fg, ss_rgb bg)
{
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    const int fw = (int)(w * frac);
    ss_fill(x, y, fw, h, fg);
    ss_fill(x + fw, y, w - fw, h, bg);
}

uint32_t ss_present(void)
{
    const uint32_t t0 = ss_cycles();
    memcpy(fb, back, sizeof(fb));
    SCB_CleanDCache();
    return ss_cycles() - t0;
}

uint8_t (*ss_gfx_back(void))[SS_LCD_W][3] { return back; }

uint32_t ss_present_rows(int y0, int y1)
{
    const uint32_t t0 = ss_cycles();
    if (y0 < 0) y0 = 0;
    if (y1 >= SS_LCD_H) y1 = SS_LCD_H - 1;
    if (y1 >= y0) {
        const size_t n = (size_t)(y1 - y0 + 1) * SS_LCD_W * 3;
        memcpy(fb[y0], back[y0], n);
        SCB_CleanDCache_by_Addr(fb[y0], (int32_t)n);
    }
    return ss_cycles() - t0;
}
