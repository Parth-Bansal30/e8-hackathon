# Sound Sense UI

The screens and LED patterns from docs/SOUND-SENSE.md §6, in plain C with no board
dependencies. The same files build on the host (tools/ui_sim renders every screen to PNG) and
on the M55.

| File | What |
|---|---|
| gfx.h / gfx.c | Anti-aliased drawing into the 480x800 B,G,R back buffer, with dirty-row tracking |
| ui.h / ui.c | Screens: home + live view, alert (important / urgent / maybe), info banner, timeline, teach, stats; 18 icons drawn in code |
| ui_led.h / ui_led.c | What the two RGB LEDs show for an alert at time t |
| ui_fonts.h / ui_fonts.c | DejaVu Sans Bold and Book as distance fields, sharp at 14-90 px (generated, see below) |
| LICENSE-DejaVu.txt | Font licence; keep it next to ui_fonts.c |

Size on the M55 at -O2: about 21 KB of code, 273 KB of font data (both in MRAM) and 17 KB of
RAM (spectrogram history and a colour table).

## Preview on a laptop

```
make -C tools/ui_sim        # writes tools/ui_sim/out/*.png and sheet.png
```

## Adding it to the board app

1. In the app's cproject.yml, add `ui/gfx.c`, `ui/ui.c`, `ui/ui_led.c` and `ui/ui_fonts.c` to
   the files, and `ui` to add-path. It does not need npu_display's fonts.h.
2. Point the UI at the back buffer once: `ui_init(back);` (the SRAM1 buffer, as in npu_display).
3. Draw a screen, then copy only the rows it touched:

   ```c
   int y0, y1;                                   /* inclusive */
   if (gfx_dirty_rows(&y0, &y1)) {
       const size_t n = (size_t)(y1 - y0 + 1) * GFX_W * 3;
       memcpy(fb[y0], back[y0], n);
       SCB_CleanDCache_by_Addr(fb[y0], n);
       gfx_dirty_reset();
   }
   ```

   A home-screen live update touches rows 140-479 (340 of 800), so it copies 490 KB, not
   1.15 MB.

4. What to call:

   | When | Call |
   |---|---|
   | Every 20 ms audio frame | `ui_spectro_push(col)`: 40 mel bands scaled 0 (quiet) to 255 |
   | About 10 times a second on home | `ui_home_update_live(&home)` |
   | Home opens or an event is added | `ui_home_draw(&home, now_ms)` |
   | Important or urgent event | `ui_alert_draw(&ev, now_ms, phase)` |
   | Info event | `ui_home_draw` then `ui_banner_draw(&ev, now_ms)` |
   | Timeline, teach, stats screens | `ui_timeline_draw`, `ui_teach_draw`, `ui_stats_draw` |
   | Every 10-20 ms during an alert | `ui_led_pattern(urgency, cat, maybe, ms_since_alert, led)`, then write the 6 LED GPIOs |

   For urgent alerts, flip `phase` every 500 ms and redraw. That is one flash per second, under
   the 3 Hz photosensitive-seizure threshold. Don't make it faster.

5. Strings for labels can use `UI_DOT` (·) and `UI_TIMES` (×) from ui_fonts.h.

## Regenerating the fonts

```
python -I tools/ui_sim/gen_font.py DejaVuSans-Bold.ttf DejaVuSans.ttf
```

This needs Pillow and numpy. The .ttf files and LICENSE_DEJAVU ship with matplotlib
(`matplotlib/mpl-data/fonts/ttf/`). Glyphs are stored at a 40 px line height with a 4 px margin.
To save MRAM, lower `H` in the script: 32 px cuts the font data to about 175 KB.
