# Devpost gallery: images and captions

Upload in this order; the first is the thumbnail. All are 1500x1000 (3:2, the ratio Devpost
recommends) PNGs under 400 KB. Rebuild them with `python3 docs/demo/gallery/make_gallery.py` after
`make -C tools/ui_sim` if the screens change.

The phone screens in these images are **UI simulator renders** (`tools/ui_sim`, the firmware's own
UI code drawn on a laptop), so the values on them are samples. Every number printed *outside* the
phone screens was measured on the board ([GATES-results.md](../GATES-results.md)). Each caption
says which is which.

| # | File | Caption (paste into Devpost) |
|---|---|---|
| 1 | [gallery/01_cover.png](gallery/01_cover.png) | Sound Sense: home sound alerts for deaf and hard-of-hearing people, on the Alif E8. Screens are UI simulator renders with sample values. |
| 2 | [gallery/02_architecture.png](gallery/02_architecture.png) | Architecture: features, matching, fusion and UI on the Cortex-M55; the int8 encoder on the Ethos-U85 through ExecuTorch. Numbers measured on the board. |
| 3 | [gallery/03_numbers.png](gallery/03_numbers.png) | Measured on the DK-E8, release build: 1.00 ms per embedding on the NPU, CPU active 12.6% in a quiet room, 0 audio dropped in 1,101 flash saves. |
| 4 | [gallery/04_teach.png](gallery/04_teach.png) | Teaching a sound on the device: five checked examples, a test match, saved to flash. Simulator renders; sample values. |
| 5 | [gallery/05_felt_vs_heard.png](gallery/05_felt_vs_heard.png) | Felt vs heard: the accelerometer tells a knock on this table from a knock on the TV. 21 of 22 knocks felt at 20 cm (measured). |
| 6 | [gallery/06_self_test.png](gallery/06_self_test.png) | Boot self-test: mics, NPU against a reference, saved sounds, IMU and lights. Ready 1.5 s after reset on the board; the renders show sample times. |
| 7 | [gallery/07_alerts_timeline.png](gallery/07_alerts_timeline.png) | Honest alerts that learn: "Maybe" when unsure; LEFT marks a wrong alert, which becomes a counter-example. Simulator renders; sample values. |
| 8 | [gallery/08_more.png](gallery/08_more.png) | "What was that?" offers to teach an unknown sound; settings on the board: night mode, mics off, forget sounds. Simulator renders; sample values. |

## Still to add (owner, from the board)

Photos of the real board beat renders, and only a photo can show live numbers. Two would help most:

1. **The board on the table** by a door, screen on, LEDs lit if possible. Use it as the thumbnail
   instead of 01 if it looks good.
   Caption: "Sound Sense running on the Alif Ensemble E8 DevKit."
2. **The real Stats page** (joystick RIGHT), sharp enough to read.
   Caption: "Stats page, live from the board: NPU time, CPU active %, NPU busy %, save time, model size."

A frame grabbed from the demo video works for both. Shoot in landscape, 3:2 or 16:9.

## Left out on purpose

- `screens/10_stats.png`: its simulator values (1.24 ms NPU, 212 ms alert delay, 132 KB model,
  96 ms save) contradict the measured ones. Use a photo of the real Stats page instead.
- `screens/04_alert_urgent_a.png`, `05_alert_urgent_b.png`: they show "Smoke alarm", but telling
  smoke from CO isn't built yet. On the board, the urgent sounds are glass breaking and siren.
- `screens.png` (the older contact sheet): it includes the same Stats sample values.

The renders in 07 and 08 come from the 9:45-9:50 AM UI (fixing mistakes, "What was that?", the
settings menu); their footers match the board's joystick controls. Knob sensitivity
(`screens/25_knob_sensitivity.png`) is left out until someone has turned the knob at the board.
