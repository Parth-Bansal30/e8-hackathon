# Devpost gallery: images and captions

Upload in this order (9 images); the first is the thumbnail. All are 1500x1000 (3:2, the ratio Devpost
recommends) PNGs, each under 400 KB except image 2 (photos, 1.1 MB; Devpost allows 5 MB). Rebuild them with `python3 docs/demo/gallery/make_gallery.py [DejaVu font folder]` after
`make -C tools/ui_sim` (and copying its `out/` renders to `screens/`) if the screens change.
Rebuilt at 1:21 PM for the judged firmware (8 examples, 437 ms alerts, Sounds page, snooze).

The phone screens in these images are **UI simulator renders** (`tools/ui_sim`, the firmware's own
UI code drawn on a laptop), so the values on them are samples. Every number printed *outside* the
phone screens was measured on the board ([GATES-results.md](../GATES-results.md)). Each caption
says which is which.

| # | File | Caption (paste into Devpost) |
|---|---|---|
| 1 | [gallery/01_cover.png](gallery/01_cover.png) | Sound Sense: home sound alerts for deaf and hard-of-hearing people, on the Alif E8. Screens are UI simulator renders with sample values. |
| 2 | [gallery/09_on_the_board.png](gallery/09_on_the_board.png) | Running on the DK-E8 (photos, judged build): the home screen with a knock just felt, and the Stats page live: 1.00 ms per embedding, CPU busy 12.2%, model 234 KB in MRAM, 0 audio dropped. Its 809 ms alert delay is for "Maybe" knocks; clear built-in matches alert in a median 437 ms. |
| 3 | [gallery/02_architecture.png](gallery/02_architecture.png) | Architecture: features, matching, fusion and UI on the Cortex-M55; the int8 encoder on the Ethos-U85 through ExecuTorch. Numbers measured on the board. |
| 4 | [gallery/03_numbers.png](gallery/03_numbers.png) | Measured on the DK-E8, release build: 1.00 ms per embedding on the NPU, CPU active 12.6% in a quiet room, clear matches alerted in a median 437 ms, 0 audio dropped in 1,101 flash saves. |
| 5 | [gallery/04_teach.png](gallery/04_teach.png) | Teaching a sound on the device: eight checked examples (one unlike the others is flagged and redone), a test match, saved to flash. Simulator renders; sample values. |
| 6 | [gallery/05_felt_vs_heard.png](gallery/05_felt_vs_heard.png) | Felt vs heard: the accelerometer tells a knock on this table from a knock on the TV. 21 of 22 knocks felt at 20 cm (measured). |
| 7 | [gallery/06_self_test.png](gallery/06_self_test.png) | Boot self-test: mics, NPU against a reference, saved sounds, IMU and lights. Ready 1.5 s after reset on the board; the renders show sample times. |
| 8 | [gallery/07_alerts_timeline.png](gallery/07_alerts_timeline.png) | Honest alerts that learn: "Maybe" when unsure; LEFT marks a wrong alert, which becomes a counter-example. Simulator renders; sample values. |
| 9 | [gallery/08_more.png](gallery/08_more.png) | "What was that?" offers to teach an unknown sound; Settings > Sounds turns each sound on or off and sets its urgency; DOWN on an alert snoozes it for 10 min. Simulator renders; sample values. |

## Board photos

The raw photos are in [board/](board/): `board_table.jpg` (taken with the screen sideways) and
`stats_page.jpg` (portrait). Gallery image 2 shows both upright in 3:2. The thumbnail stays the
rendered cover (1), which is sharper than the photos.

## Left out on purpose

- `screens/10_stats.png`: its simulator values (1.24 ms NPU, 212 ms alert delay, 132 KB model,
  96 ms save) contradict the measured ones. Use a photo of the real Stats page instead.
- `screens/04_alert_urgent_a.png`, `05_alert_urgent_b.png`: they show "Smoke alarm", but telling
  smoke from CO isn't built yet. On the board, the urgent sounds are glass breaking and siren.
- `screens.png` (the older contact sheet): it includes the same Stats sample values.

The renders in 07 and 08 come from the 9:45-9:50 AM UI (fixing mistakes, "What was that?", the
settings menu); their footers match the board's joystick controls. Knob sensitivity
(`screens/25_knob_sensitivity.png`) is left out until someone has turned the knob at the board.
