# Sound Sense: project status

Snapshot: Sunday 2026-10-18, about 9 AM PDT, branch `gates` (after `c6bf638`). Submission deadline
4:30 PM PDT. Feature IDs and priorities come from [SOUND-SENSE.md](SOUND-SENSE.md); the measurements
behind every "Done" are in [GATES-results.md](GATES-results.md).

## Summary

| Priority | Total | Done | Partial | Not started |
|---|---|---|---|---|
| **P0** (must-have product) | 16 | **15** | 1 (T2) | 0 |
| **P1** (makes it good) | 16 | 7 | 5 | 4 |
| **P2** (nice to have) | 20 | 0 | 4 | 16 |
| **P3** (stretch) | 7 | 0 | 0 | 7 |

The must-have product works on the board end to end: listen, recognise, teach, alert, save to
flash, self-test at boot. The weak spot is accuracy: built-in sounds played from a laptop speaker
were recognised 54% of the time, and taught sounds were accepted 38% of the time offline at 5
examples (they work well live; the offline test uses unseen clips from strangers' recordings).

## Gates (feasibility tests)

| Gate | Result | Left to do |
|---|---|---|
| G1 encoder | SOFT (borderline): 5-way 5-shot 75.8% int8 | 8 examples per taught sound |
| G2 knock felt | SOFT: felt at 20 cm (21 of 22), not at 50 cm | Re-measure alignment after the timestamp fix (needs knocks) |
| G3 live | SOFT, stopped early: built-ins 54% from the laptop speaker | Taught, rejection and false-alarm runs; re-measuring the built-ins on the board. All need sound in the room |
| C1 NPU | GO: all 14 ops on the NPU, 1.0 ms, matches the host | — |
| C2 flash | GO: 1,101 saves, 0 audio dropped, restore 2.2 ms | — |
| C3 alerts/screen | Rows 1-4 pass: alert on screen 19 ms after the decision, strobe + stress 0 dropped, live view 10 fps | Backlight dimming (on/off works) |
| C4 mics | GO for left/right direction | — |
| C5 power | Done: CPU active 12.6% in a quiet room (target 10%), NPU idle | Measure during continuous speech (needs sound) |

## Every feature

**Hearing**

| ID | Feature | Pri | Status | Notes |
|---|---|---|---|---|
| H1 | Built-in sounds (7 alerting + speech, background) | P0 | Done | 54% live from the laptop speaker, 88% offline; re-measuring on the board needs sound |
| H2 | Teach your own sound | P0 | Done | Works live; 38% offline acceptance at 5 examples, so 8 is planned |
| H3 | Reject unknown sounds | P0 | Done | 79% offline; live run not measured |
| H4 | Smoke vs CO from the beep pattern | P1 | Not started | |
| H5 | Ongoing sounds ("for 6 min") | P1 | Partial | Repeats within 30 s merge with a count and duration; no "still sounding" |
| H6 | "What was that?" with teach-this | P1 | Not started | Unknown loud sounds are only logged |
| H7 | Your name called | P1 | Partial | "My name" can be taught like any sound; not tested on speech |
| H8 | Talking-nearby indicator | P2 | Partial | "Speech" appears in the live top 3 only |
| H9 | Noise meter | P2 | Partial | Live level bar only |
| H10 | Teach from one long recording | P2 | Not started | |
| H11 | Improve a built-in with your examples | P2 | Not started | Developer re-measure command over J-Link only |
| H12 | Left/right direction | P3 | Not started | Mic geometry measured (C4 GO) |

**Felt (vibration sensor)**

| ID | Feature | Pri | Status | Notes |
|---|---|---|---|---|
| F1 | Knock felt vs heard only | P1 | Done | A heard-only knock is a banner ("TV?"), not a full alert; IMU timestamp bug fixed (frame times from the sensor clock) |
| F2–F5 | Vibration signatures, moved-device check, tap to dismiss, vibration-only events | P2–P3 | Not started | |

**Alerts**

| ID | Feature | Pri | Status | Notes |
|---|---|---|---|---|
| A1 | Alert card (icon + word + time) | P0 | Done | |
| A2 | LED pattern per sound | P0 | Done | Confirmed by eye |
| A3 | Three urgency levels | P0 | Done | Urgent strobe at 1 flash per second (photosensitivity-safe) |
| A4 | Acknowledge | P0 | Done | Joystick only; touch isn't wired up |
| A5 | Urgent alerts repeat and escalate | P1 | Done | Urgent alerts stay until a press; after 30 s the strobe doubles to 2 flashes/s (under 3 Hz) and "No response for N" shows |
| A6 | Night mode | P1 | Done | Menu (DOWN) → Night mode. Home screen goes dark 15 s after the last press (backlight off, live view and live encoder off: CPU active 8.4% vs 12.2%); info sounds only logged; important/urgent alerts light it; a press in the dark only wakes it. Backlight off not yet confirmed by eye |
| A7 | "Maybe: doorbell?" | P1 | Done | |
| A8 | Snooze a sound | P2 | Not started | |
| A9 | Home / Night / Away modes | P2 | Not started | The screen just says "Home mode" |

**Teaching**

| ID | Feature | Pri | Status | Notes |
|---|---|---|---|---|
| T1 | Guided teach flow | P0 | Done | 5 examples, each shown with its dB over the room |
| T2 | Quality check per example | P0 | **Partial** | "Too quiet" and "clipped" done; "unlike the others" and "background too loud" missing |
| T3 | Test right after teaching | P0 | Done | |
| T4 | Fix mistakes from the timeline | P1 | Done | Timeline (selected top row) or alert: LEFT = wrong, RIGHT = right. Wrong keeps the sound's embedding as a counter-example: a sound closer to it than to the profile is vetoed (saved in flash for taught sounds; built-ins until restart). Right on a taught sound adds an example and rebuilds its profile and threshold. Tested silently with the boot test clip (host command 15) |
| T5 | Sensitivity on the knob | P1 | Not started | |
| T6 | Similar-sound warning | P1 | Not started | |
| T7 | Manage sounds (rename, delete, factory reset) | P1 | Partial | Menu: forget one taught sound or all of them (press twice to confirm), saved to flash. No rename, urgency or built-in on/off |

**Timeline**

| ID | Feature | Pri | Status | Notes |
|---|---|---|---|---|
| L1 | Today's timeline | P0 | Done | |
| L2–L4 | Away summary, daily counts, event detail | P2 | Not started | |
| L5 | Real clock | P2 | Partial | Set over J-Link only; no RTC |
| L6 | Export the log | P3 | Not started | |

**Setup and system**

| ID | Feature | Pri | Status | Notes |
|---|---|---|---|---|
| S1 | Boot self-test, "Ready" screen | P0 | Done | Mics, NPU vs reference, saved sounds, IMU, lights; ready 1.5 s after reset |
| S2 | Saved sounds survive power loss | P0 | Done | OSPI flash, two copies with CRC32 |
| S3 | Live spectrogram and top 3 | P0 | Done | Shows "Quiet: NPU resting" when the NPU is gated off |
| S4 | Stats page | P0 | Done | NPU ms, CPU active %, NPU busy %, save ms, model size |
| S5 | Noise-floor calibration | P1 | Partial | The floor tracks the room continuously; no explicit 10 s calibration |
| S6 | Privacy screen and mic mute | P1/P2 | Partial | "Nothing is recorded" footer only |
| S7 | First-run wizard | P2 | Not started | |
| S8 | Knock placement check | P2 | Not started | |

**Accessibility, efficiency, integration**

| ID | Feature | Pri | Status | Notes |
|---|---|---|---|---|
| X1 | High contrast, icon + word, colour-blind-safe | P0 | Done | |
| X2 | Touch, joystick or knob for everything | P2 | Partial | Joystick only |
| X3 | Flip the screen from the IMU | P2 | Not started | |
| X4 | Brightness on the knob | P2 | Not started | |
| E1 | NPU only when it's loud | P1 | Done | Live encoder runs only above floor + 6 dB, and 300 ms after |
| E2 | Sleep between audio blocks (WFI) | P1 | Done | |
| E3 | Faster FFT (CMSIS-DSP) | P2 | Not started | |
| E4 | Always-on HE core | P3 | Not started | |
| I1–I3 | USB/Ethernet alerts, bed-shaker relay, SD log | P3 | Not started | Hardware untested |

## Open items outside the feature list

| Item | Status | Notes |
|---|---|---|
| Submission: README, Devpost write-up, demo video | README done; the rest in a separate session | Brief: [HANDOFF-submission.md](HANDOFF-submission.md) |
| Alert delay | Open | About 0.85 s against a 0.5 s target. Trying 0.4–0.5 s needs the torch environment and datasets (not on the board laptop) |
| Built-in accuracy | Open | Needs sound in the room |
| IMU timestamp bug | Fixed | Backward jumps under stalls 69 ms -> 0; sensor clock measured at +0.68% |
| 8 examples + T2 checks | Parked until 10:30 AM | HANDOFF-next.md steps 4 and 6 |
