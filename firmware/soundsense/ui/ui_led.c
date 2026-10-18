/* LED patterns for alerts. See ui_led.h. */
#include "ui_led.h"

static ui_led cat_led(ui_category c)
{
    switch (c) {
    case UI_CAT_SAFETY:    return (ui_led){true, false, false};   /* red */
    case UI_CAT_DOOR:      return (ui_led){false, false, true};   /* blue */
    case UI_CAT_FAMILY:    return (ui_led){true, false, true};    /* magenta */
    case UI_CAT_ANIMAL:    return (ui_led){true, true, false};    /* yellow */
    case UI_CAT_APPLIANCE: return (ui_led){false, true, true};    /* cyan */
    case UI_CAT_WATER:     return (ui_led){false, true, true};    /* cyan */
    case UI_CAT_VOICE:     return (ui_led){false, true, false};   /* green */
    default:               return (ui_led){true, true, true};     /* white */
    }
}

void ui_led_pattern(ui_urgency u, ui_category c, bool maybe, uint32_t t_ms, ui_led led[2])
{
    static const ui_led off = {false, false, false}, white = {true, true, true};
    const ui_led col = cat_led(c);
    led[0] = led[1] = off;

    if (maybe) {                          /* one short blink every 4 s */
        if (t_ms % 4000 < 150) led[0] = col;
        return;
    }
    switch (u) {
    case UI_URGENT: {                     /* 2 Hz, the LEDs alternate colour and white */
        const bool a = (t_ms % 500) < 250;
        led[0] = a ? col : white;
        led[1] = a ? white : col;
        break;
    }
    case UI_IMPORTANT: {                  /* double blink once a second, both LEDs */
        const uint32_t p = t_ms % 1000;
        if (p < 120 || (p >= 240 && p < 360)) led[0] = led[1] = col;
        break;
    }
    case UI_INFO:                         /* one blink every 2 s, for the first 20 s */
        if (t_ms < 20000 && t_ms % 2000 < 150) led[0] = col;
        break;
    }
}
