/* LED patterns for alerts (A2): what the two RGB LEDs show at a given moment.
 *
 * Each LED channel is a plain GPIO (on/off), so the colours are the 7 on/off mixes. The app calls
 * ui_led_pattern() every 10-20 ms with the time since the alert started and writes the result to
 * the LED pins (docs/bringup-notes.md lists them). Rhythm carries the urgency and colour the
 * category, so the meaning survives colour blindness. Flash rates stay at or below 2 Hz. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { bool r, g, b; } ui_led;

/* led[0] and led[1] for an alert of this urgency and category, t_ms after it started.
 * maybe = low-confidence match (one slow blink). */
void ui_led_pattern(ui_urgency u, ui_category c, bool maybe, uint32_t t_ms, ui_led led[2]);

#ifdef __cplusplus
}
#endif
