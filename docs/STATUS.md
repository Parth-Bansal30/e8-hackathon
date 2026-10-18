# Sound Sense: project status

Snapshot: Sunday 2026-10-18, about 12:35 PM PDT, branch `gates` (feature work stopped). Submission deadline
4:30 PM PDT. Feature IDs and priorities come from [SOUND-SENSE.md](SOUND-SENSE.md); the measurements
behind every "Done" are in [GATES-results.md](GATES-results.md).

## Summary

| Priority | Total | Done | Partial | Not started |
|---|---|---|---|---|
| **P0** (must-have product) | 16 | **16** | 0 | 0 |
| **P1** (makes it good) | 16 | 14 | 1 | 1 (H4) |
| **P2** (nice to have) | 20 | 2 | 3 | 15 |
| **P3** (stretch) | 7 | 0 | 0 | 7 |

The must-have product works on the board end to end: listen, recognise, teach, alert, save to
flash, self-test at boot. Settings cover sounds on/off, urgency, renaming, room calibration and
snooze. The weak spot is accuracy:
- Built-in sounds played from a laptop speaker were recognised 54% of the time.
- Taught sounds were accepted 45% of the time offline at 8 examples (38% at 5). The offline test
  uses unseen clips from strangers' recordings. Live they work well: a tight Doorbell teach was
  recognised in 4 of 5 trials.

**Final checks at 12:30 PM** (release build flashed from the sources at `ed20a3e`): the full-app
smoke test passed 16/16 with 0 audio blocks dropped and Ready in 1.28 s, and the feature test passed
18/18. Not yet checked by eye or hand: night mode turning the backlight off, and the knob.

## Gates (feasibility tests)

| Gate | Result | Left to do |
|---|---|---|
| G1 encoder | SOFT (borderline): 5-way 5-shot 75.8% int8 | Done: 8 examples per taught sound (offline acceptance 38% -> 45%) |
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
| H2 | Teach your own sound | P0 | Done | Works live; 8 examples: offline acceptance 45% (38% at 5), untaught rejection 77% |
| H3 | Reject unknown sounds | P0 | Done | 79% offline; live run not measured |
| H4 | Smoke vs CO from the beep pattern | P1 | Not started | |
| H5 | Ongoing sounds ("for 6 min") | P1 | Done | Repeats within 30 s merge with a count and duration. Heard again in the last 15 s: "still sounding" on the alert, home and timeline, cleared 15 s after the last repeat. Tested on the board (`tools/ss_t7_test.py`) |
| H6 | "What was that?" with teach-this | P1 | Done | A sound 15 dB over the room that matches nothing (and isn't speech or room noise) gets a soft banner, at most once a minute, and a timeline entry; UP opens teaching with that sound as example 1 |
| H7 | Your name called | P1 | Partial | "My name" can be taught like any sound; not tested on speech |
| H8 | Talking-nearby indicator | P2 | Done (not seen live yet) | Home: a yellow "Someone is talking nearby" over the live view while Speech is the best match over its threshold (and 3 s after). Rendered in the UI simulator; needs speech in the room to see on the board |
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
| A8 | Snooze a sound | P2 | Done | DOWN on an alert: that sound is logged but doesn't alert for 10 min, with a notice. Settings > Sounds shows "Snoozed" (press to end it). Tested on the board with injected alarms, 4/4 (`tools/ss_t7_test.py`) |
| A9 | Home / Night / Away modes | P2 | Not started | The screen just says "Home mode" |

**Teaching**

| ID | Feature | Pri | Status | Notes |
|---|---|---|---|---|
| T1 | Guided teach flow | P0 | Done | 8 examples in two rows of 4, each shown with its dB over the room |
| T2 | Quality check per example | P0 | Done | Too quiet (rejected), clipped, loud room (floor over −40 dBFS: kept, flagged orange), unlike the others (flagged when redoing it would raise the threshold by 0.10 or more; UP makes it again). Tested on the board without sound (host command 17); "loud room" not triggered yet |
| T3 | Test right after teaching | P0 | Done | |
| T4 | Fix mistakes from the timeline | P1 | Done | Timeline (selected top row) or alert: LEFT = wrong, RIGHT = right. Wrong keeps the sound's embedding as a counter-example: a sound closer to it than to the profile is vetoed (saved in flash for taught sounds; built-ins until restart). Right on a taught sound adds an example and rebuilds its profile and threshold. Tested silently with the boot test clip (host command 15) |
| T5 | Sensitivity on the knob | P1 | Done (not checked by hand) | The blue R79 knob moves every threshold by up to ±0.10 (one setting for all sounds, not per sound); a notice with a bar shows when it's turned; menu shows the value. Wherever the knob was left, the board starts as taught until it's turned. The ADC read 0% at boot; turning it hasn't been tried yet |
| T6 | Similar-sound warning | P1 | Done | "Sounds like X (NN%): may mix up" when the new profile is within cosine 0.70 of any other; saving still allowed, no merge. Tested on the board (the dog test clip vs "Dog barking", 0.79) |
| T7 | Manage sounds (rename, delete, factory reset) | P1 | Done | Settings > Sounds: each built-in on or off (off: not shown or logged), each taught sound Info / Important / Urgent, and RIGHT renames it to the next free preset name; all saved in the flash and restored at boot. Forget one taught sound or all (press twice). Tested on the board, 14/14 (`tools/ss_t7_test.py`) |

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
| S5 | Noise-floor calibration | P1 | Done | The floor tracks the room continuously. Settings > Calibrate room: 10 s, then the floor is the median 20 ms level; shows the result and the level sounds need. Tested: −50.5 dB, matching the running floor |
| S6 | Privacy screen and mic mute | P1/P2 | Done | Menu: Microphones On/Off (off: nothing classified or shown, header says "Mic off"), Privacy: "Nothing is recorded or sent. Sound is kept 1 s". From the menu, not a long press |
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
| Alert delay | Two-stage decision on the board: clear built-in alerts in a median 437 ms | On the board, G3 built-ins: 18 of 30 recognised sounds alerted early, median 437 ms from onset to screen, 0 wrong; recognition unchanged at 54%. Others still about 0.87 s. Offline: a plain 0.45 s delay costs about 9 points on built-ins. A two-stage decision (early at 0.4 s when the match is clear, otherwise 0.8 s) alerts 55% of built-in sounds at about 0.45 s with no loss in recognition and +1.5 points of false alerts. See [GATES-results.md](GATES-results.md#alert-delay-offline) |
| Built-in accuracy | Open | Needs sound in the room |
| IMU timestamp bug | Fixed | Backward jumps under stalls 69 ms -> 0; sensor clock measured at +0.68% |
| IMU `times back` while J-Link is attached | Fixed | Every J-Link detach stops the cycle counter until it is re-armed; a detach during a long draw lost up to the whole draw (times went back up to 108 ms, clock reading spiked to +4%). Re-armed every 1 ms in SysTick now: 30 s of full redraws with the logger running, 0.55 ms max (was 31.8 ms). Only happens with a debugger attached |
| 8 examples + T2 checks + T6 | Done; tested live | Doorbell clip from the Mac. At volume 69 the first teach found two bugs, both fixed ("loud room" fired during the sound itself; T2 missed two stray shots), and the second was loose (threshold 0.50): 1 of 5 recognised, Siren confusions. **Demo rehearsal at 12 PM, volume 44: tight teach (threshold 0.85), 4 of 5 recognised at 0.95-0.96, the 5th quieter and just under (0.82), no Siren confusions** |
| Full-app smoke test | 16/16 pass, 0 audio blocks dropped | `tools/ss_smoke.py`, no sound. The first run's 2 failures were a J-Link tool bug (stale `ss_arg`), fixed in `tools/jlink.py` |
