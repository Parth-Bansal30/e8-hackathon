# Sound Sense gate results (2026-10-18, in progress)

**Checkpoint (G1–G3):** G1 SOFT, G2 SOFT, G3 SOFT. From "What the results unlock" in `HANDOFF-gates.md`:
- **G1 SOFT:** 8 examples per taught sound; T4 (fix mistakes) becomes P0; the demo uses distinctive sounds.
- **G3 SOFT:** ship only the built-ins that pass; teaching carries the demo. Knock, alarm and baby matter most.
- **G2 SOFT:** F1 (felt vs heard) ships with "place me on the table by the door".

| Test | Result | Key numbers | Notes |
|---|---|---|---|
| G1 encoder | SOFT (borderline) | 5-way 5-shot int8 75.8% (float 75.9%); 10-way 63.0%. Taught accepted 38%, untaught rejected 79%, AUROC 0.687. Built-ins 88% recognised with 4% false accepts. 219k params, 43.2 M MACs, .pte 240 KB | Shipped ch192 (e22234c); also tried ch128 (74.1%) and ch192 with SpecAugment (72.6%). Taught-sound acceptance is the weak spot. Details in `model/out/g1_metrics.json` |
| G2 IMU | SOFT | 805 to 834 Hz through the FIFO, 0 missed, 254 µs per read, 3.5% CPU. Felt at 20 cm: 21 of 22 knocks (95%). At 50 cm: 0 of 20 hard knocks; they peak at the sensor's noise level. Phone 30 cm above the table: 2 of 21 knock sounds flagged as felt, both while the phone was being put down; the raw data shows 0 of 21. Alignment: 55% within ±10 ms, held back by an IMU timestamp bug | F1 ships with "place me on the table by the door" (S8). The bug: computed IMU frame times jump back by up to 54 ms while the sensor clock stays continuous. **Fixed (later on 10-18):** frames are now timed from the BMI323 sensor clock, tied to the CPU clock whenever a read empties the FIFO, with the sensor clock's rate measured against the CPU every 5 s (it runs +0.68% fast, which is why G2 saw 805 Hz). Under 40 back-to-back flash saves (124 ms stalls) the largest backward step between frame times went from 69 ms (old timing, same firmware, `ss_imu_old_timing` = 1) to 0. Knock alignment not re-measured (needs knocking). Tools: `tools/g2_report.py`, `tools/g2_raw.py`; log in `gates/g2_rtt.log` |
| G3 live | SOFT (short run, stopped early) | Built-ins played from the Mac speaker (ESC-50 fold 5, 8 clips each): 30 of 56 recognised (54%): alarm 6/8, knock 5/8 (4 as "maybe", helped by the table feeling the speaker), water 5/8, dog 4/8, baby 4/8, glass 3/8, siren 3/8. Taught sounds work live (confirmed by the user at the board). Decision 0.8 s after the onset, so latency is about 0.85 s against a 0.5 s target. 0 audio blocks dropped | Stopped early to save time: the taught, rejection and false-alarm rows weren't measured. Re-measuring the built-ins on the board (row 7) was tried; the second attempt caught too few examples (alarm, glass, siren, water got 0 to 2) and wasn't kept. Tools: `tools/g3_play.py`; results in `gates/g3_results.json`, `gates/g3_builtin_trained.txt`, `gates/g3_remeasure.txt` |
| C1 NPU | GO | All 14 operators on the NPU, 0 on the CPU (Vela). Boot self-test on the board: C features match Python on 0 of 1960 values off; embedding within 1 LSB of the host (cosine 0.9996), and it picks "dog" (0.79) for the held-out dog clip. NPU 1.08 ms debug / 1.00 ms release; runner 1.12 / 1.03 ms; features 0.65 / 0.30 ms per update. .bin 1,010,160 B debug / 1,015,648 B release, 48% of the 2 MB partition | `firmware/soundsense` (b905c49), the `[C1]` line in `gates/ss_rtt.log`. The .bin includes 273 KB of UI fonts and the 240 KB model. Live loop: 10 encoder runs a second, CPU busy 22% in release (75% in debug, mostly drawing), 0 audio blocks dropped |
| C2 flash | GO (rows 1 and 4 measured; rows 2 and 3 not run, judged unnecessary by the project owner) | Row 1: 101 back-to-back saves of a full-size record (8 taught sounds x 5 examples, 4,832 B, 2 sectors) while listening: 0 audio blocks dropped, 0 failed read-backs; save 223 to 266 ms (median 252, p90 261). A further 1,000-save run: 0 dropped, 0 failed, longest 304 ms (just over the 300 ms target, once). The longest blocking step (one 4 KB erase) took 124 ms, inside the 320 ms audio ring. Row 4: boot restore 2.2 ms with the full record, 0.13 ms empty; 8 of 8 sounds back after 3 J-Link resets and after a re-flash. Power pulls: not tested; a pulled save can only damage the older copy, which fails its CRC, so boot falls back to the other | `firmware/soundsense/ss_store.c`, `[STORE]` lines in `gates/c2_rtt.log`; bench: host command 12 (stand-ins), 11 (N saves), `tools/c2_power.py` for power pulls. The OSPI and flash driver code runs from MRAM (linker script), since ITCM is 97.6% full |
| C3 UI | Rows 1-4 pass; row 5 (backlight) on/off only, dimming not tried | Row 1, LEDs: both RGB LEDs showed red, green, blue and white exactly as commanded, confirmed by eye; blinks at 2, 4 and 8 Hz are visible across the room. Rows 2-4 measured with no sound played: host command 13 fakes a recognised sound, and the board times itself (release). Row 2: alert on screen 18.4 to 19.5 ms after the decision (draw 15 ms into the back buffer, copy 3.5 ms), banner 19.5 ms; target 100 ms. Row 3: urgent alert (strobe, a full redraw every 500 ms) with host command 14 forcing 10 full redraws a second on top for 12 s: 151 full redraws in 15 s, longest draw 27.8 ms + copy 3.6 ms, 0 audio blocks dropped (CPU active 37% meanwhile). Copying the whole 1.15 MB frame takes 3.5 ms, so handing the display a pointer instead wasn't needed. Row 4: live view 10.0 fps (spectrogram, level, top 3), longest update 10.3 ms, 0 dropped. Row 5: the backlight is GPIO6_1, switched on by the panel driver; night mode uses on/off. Dimming would need PWM: P6_1 can also be UTIMER4 output T1 (alt function 4), which means enabling channel 4 driver B in the shared RTE_Device.h and masking the driver's per-cycle compare interrupts. Not done | Pins as in `docs/bringup-notes.md`; LED check by `firmware/imu_bench` at boot. Rows 2-4: `tools/c3_ui.py`, `[C3]` and `[SS] draw` lines in `gates/c3_rtt.log`. Debug build not measured (release ships) |
| C4 mics | GO for left/right direction (H12) | Spacing 8 cm (ruler). The Mac speaker in line with the pair gave +3.2 samples on the left and −3.6 on the right, steady to ±0.2. That makes the maximum lag 3.4 samples (73 mm) and the channel offset −0.2. Mic 4 is the left mic. Level mic5 − mic4: −4.1 dB on hand claps, 0 to +1 dB from the speaker | See [C4 details](#c4-details) |
| C5 duty | Done; CPU active 12.6% in a quiet room (target 10%, not a gate), NPU busy 0% | Quiet room, 60 s, release: CPU awake 12.6% (outside WFI), NPU busy 0.00 to 0.05%, 0 to 0.5 encoder runs a second (live view), 0 audio blocks dropped, IMU 805 samples/s with 0 errors. Before: 100% (busy loop), NPU 10 runs/s, ~1%. Steps, each measured: WFI when no audio block waits (E2; the cycle counter keeps counting through WFI, checked at boot) -> 90%; PC sampling over J-Link found 67% in `ARM_LPGPIO_GetValue`: the joystick's low-power GPIO read costs ~130 us, 5 per pass. Polling it every 10 ms with one port read -> 21%; live view skips unchanged rows and scrolls the spectrogram instead of 16,640 fills -> 14%; IMU read every 10 ms instead of 5 (each busy-waits on I3C) -> 12.6%. NPU gate (E1): the live encoder runs only on the home screen while the level is above the noise floor + 6 dB and 300 ms after; sound decisions always run their own inference, so G3 detection can't change. Not measured: continuous speech (no test sounds in the room) | `[SS] stats` lines in `gates/c5_rtt.log` (`active`, `npu busy`, passes/sleeps per second). E3 (CMSIS-DSP FFT) not done |

## Alert delay (offline)

HANDOFF-next.md item 11, done on the old laptop (et-venv, float encoder as shipped). Script: `model/delay_sweep.py`. Results: `model/out/delay_sweep_seed{5,6,7}.json` with logs; the numbers below are the mean of the 3 seeds.

**Method.** Each clip goes through a host port of the board's onset detector (`ss_audio.c`) and main loop (HOLDOFF, the 8 dB gate). The 1 s window the board would encode at each delay is then rerun through the G1 measurements.
- Room: 1 s of lead-in, pink noise under the clip, and the clip's loudest 20 ms set 10 to 25 dB over it. G3 on the board saw +8 to +22 dB.
- Teaching: taught sounds are taught and tested at the same delay, since teaching also goes through `decide()`.
- Built-in profiles: as shipped (each clip's loudest 1 s).
- Pool size: the built-in pool is small (56 fold-5 clips), so its numbers move by 2 to 4 points between rooms.
- What transfers to the board: absolute numbers don't (G3 live got 54% where offline gave 85%); the differences between delays are the result.

**One decision at a shorter delay** (just changing `DECIDE_DELAY`). The alert shows about 0.05 s after the delay.

| Delay | Built-ins recognised | Other sounds alerting | 5-way 5-shot | Taught accepted | Untaught rejected | AUROC |
|---|---|---|---|---|---|---|
| 0.8 s (today) | 83.8% | 5.1% | 69.9% | 37.9% | 68.8% | 0.634 |
| 0.6 s | 82.0% | 4.8% | 68.9% | 41.3% | 65.7% | 0.637 |
| 0.5 s | 77.6% | 5.3% | 69.8% | 44.0% | 64.4% | 0.644 |
| 0.45 s | 74.7% | 6.6% | 69.2% | 44.9% | 61.7% | 0.639 |
| 0.4 s | 74.9% | 6.2% | 68.7% | 45.4% | 60.0% | 0.635 |
| 0.3 s | 66.4% | 4.3% | 67.3% | 47.2% | 56.8% | 0.636 |

- **Built-ins:** 0.5 s costs about 6 points, 0.45 s about 9.
- **Taught sounds:** separation (AUROC) and few-shot accuracy barely move. The threshold rule trades rejection for acceptance: −7 points of untaught rejection at 0.45 s.
- **G1 framing for reference:** loudest 1 s, no room: 75.9% few-shot, 85.3% built-ins.

**Two-stage decision.** Decide at 0.4 s when the best match is a built-in clearing its threshold by a margin; otherwise wait for 0.8 s as today.
- Recognition doesn't change: built-ins (the 56 clips) are recognised 79.5% either way.
- Wrong at 0.4 s: 0.7% of built-in sounds, i.e. a different built-in than the 0.8 s decision would give.

| Early when | Built-in sounds alerted at ~0.45 s | Mean alert delay (recognised) | Other sounds alerting | Untrained sounds alerting |
|---|---|---|---|---|
| Never (today) | 0% | 0.85 s | 5.1% | 4.5% |
| Over threshold | 64.2% | 0.53 s | 8.9% | 7.0% |
| Threshold + 0.05 (the alerts that aren't "Maybe") | 54.7% | 0.57 s | 6.6% | 5.8% |
| Threshold + 0.10 | 43.8% | 0.63 s | 5.3% | 5.3% |

0.45 s and 0.5 s as the early delay came out the same or slightly worse (`two_stage` in the JSON).

**Side finding:** built-in profiles made from windows framed the way the board frames them, instead of the loudest 1 s:
- At 0.8 s: built-ins 83.8% → 90.2%; other sounds alerting 5.1% → 6.0%; untaught rejection 68.8% → 66.6%.
- At shorter delays they let through far more false alerts (9.6% at 0.5 s), so this only helps at 0.8 s.
- It's an `export_ss.py` change (a new `ss_builtin_profiles.h`), but G3 on the board is what should decide it.

**Recommendation:** don't shorten `DECIDE_DELAY`. A two-stage decision gets most built-in alerts to about 0.45 s at no cost in recognition:
- Early check at 0.4 s, margin 0.05: 55% of built-in sounds alert early; false alerts +1.5 points.
- Margin 0.10 instead: 44% early, and false alerts no higher than today.

**Firmware: flashed and tested on the board (10:50 AM): clear built-in alerts reach the screen in a median 437 ms, recognition unchanged** (`decide_early()` in `main.cpp`):
- At 0.4 s after the onset (`EARLY_DELAY`), one extra encoder run (about 1 ms) checks the match.
- It decides at once only if all of these hold:
  - the best match is an alerting built-in
  - it clears its threshold (knob included) by `MAYBE_MARGIN` (0.05), so it would not be a "Maybe"
  - it isn't vetoed by a sound marked wrong (T4)
  - it's 8 dB over the room in the 1 s the encoder saw (its own SNR window; `snr_of()` at 0.8 s still reads onset − 0.2 s to + 0.8 s)
- Everything else waits for 0.8 s as before: taught sounds, "Maybe", felt-only knocks, teaching and `[CAL]`.
- Each early decision logs `[SS] t=... early, N ms after the onset` before the usual decision line, so `g3_play.py` parses it unchanged.
- Release build: ITCM 254,944 B (97.25%, +720 B).
- Silent check after flashing (`gates/early_rtt.log`):
  - Ready screen: ready after 1,479 ms, every check OK.
  - `[C1]`: 0 of 1960 inputs differ, output within 1 LSB, cosine 0.9996, "dog" 0.79, NPU 1.0 ms. Same as before.
  - Quiet room: CPU active 12.0 to 13.1%, 0 audio blocks dropped.
  - Room noises gave 3 decisions; all were weak matches (best 0.20 to 0.41 against a threshold of 0.50), and all waited for 0.8 s as they should.
- IMU `times back` read 95 µs, then 1.7 ms, then 3.0 ms in the first 40 s. The early path didn't cause it. **Found and fixed at 11:25 AM:** see [IMU times back](#imu-times-back-with-j-link-attached).
- **G3 built-in run with the Mac speaker** (same 56 fold-5 clips, volume 88 as found; `gates/g3_builtin_early.txt`, `g3_results.json` entry at 10:51):
  - Recognised: 30 of 56 (54%), the same as the run before the change.
  - Per class (before → now): knock 5 → 1, dog 4 → 6, baby 4 → 4, alarm 6 → 6, glass 3 → 2, siren 3 → 3, water 5 → 8.
  - The knock drop isn't from the early path: none of the knocks were felt this time (5 were before, from the speaker shaking the table), and two knocks that matched "Knock" under the threshold went to "unknown". Possible reasons: a different laptop or placement, or the IMU `times back` above.
  - Early: 18 of the 30 recognised sounds alerted early (60%; offline said 55%): dog 5, water 6, alarm 5, knock 1, glass 1. Wrong early alerts: 0.
  - Latency: early decisions came 403 to 442 ms after the onset; on screen a median 437 ms after the onset (403 to 490 ms). The rest still decide at 0.8 s and show about 65 ms later. Mean over recognised sounds: about 0.61 s, down from about 0.87 s.
  - Draw time: decision to screen took 30 to 75 ms here, against 18 to 20 ms in C3 with fake detections. Not looked into.

## Teaching: 8 examples, T2, T6

Done at 11 AM. Offline numbers come from `model/shots_eval.py` (G1 pools, float encoder, 600 episodes; `model/out/shots_eval.log`).

**8 examples per taught sound** (`SHOTS` 5 → 8; the teach screen shows two rows of 4):

| Examples | Taught accepted | Untaught rejected | AUROC |
|---|---|---|---|
| 5 | 38.9% | 78.2% | 0.687 |
| 8 | 44.6% | 77.1% | 0.709 |

**T2 "unlike the others":** a shot is flagged when making it again would raise the profile's threshold by 0.10 or more. The threshold is the lowest leave-one-out similarity − 0.05, so one stray example drags it down for everything.
- A plain "0.15 under the median" rule flagged 88% of ESC-50 teaches. Those classes are many different recordings, far more varied than one doorbell taught 8 times, so that rule was dropped.
- The threshold rule flags 0.4% of ESC-50 teaches. A live, same-source teach is tight (the keys preview: threshold 0.81), and there one stray example would drop the threshold to 0.50.
- The flagged shot turns orange ("unlike"), and UP makes it again; saving is still allowed.

**T2 "loud room":** a shot taken while the room floor is over −40 dBFS (a quiet room here is about −50) is kept but flagged orange. Rejecting it would make teaching impossible in a noisy hall.

**T6 "Sounds like …":** the new profile is compared with every other profile; at cosine 0.70 or more the screen says "Sounds like X (NN%): may mix up".
- Two 8-example profiles of the same class score a median 0.79 (p10 0.59).
- A held-out class against the closest built-in scores a median 0.37 (p90 0.56); 0.5% reach 0.70.
- Two different held-out classes score a median 0.00 (p90 0.49).

**On the board, no sound** (host command 17; `gates/teach8_rtt.log`; screenshots `gates/teach8_board_flag.png`, `gates/teach8_board_redo.png`):
- Seven shots of the boot test clip and one of the room: leave-one-out 0.99 ×7 and 0.00. T2 flagged shot 8 (threshold 0.850 without it, 0.500 with it), and T6 said "Sounds like Dog barking" (0.79; the test clip is a dog).
- UP, then one more clip shot: shot 8 was replaced and the threshold went to 0.850.
- Cancelled without saving. Release build: ITCM 97.31%; the new teach code is in MRAM.

**Live, the doorbell clip from the Mac speaker at volume 69** (`gates/ss_rtt.log`, `gates/g3_teach8.txt`, `gates/g3_trials8.txt`):
- **First teach:** 6 shots were tight (leave-one-out 0.88-0.92) and 2 were stray (0.18 and 0.02). The test match was 0.817.
  It showed two bugs, both fixed and flashed at 11:10 AM:
  - "Loud room" flagged 6 of 8 shots. The room floor rises while the doorbell plays, so the check now
    measures the 0.2 s before each sound (`level_db()`).
  - T2 flagged neither stray shot: with two of them, leaving out one doesn't raise the threshold. T2
    now tries leaving out the worst one, then the worst two. Without both, the threshold would be
    about 0.83 instead of 0.50.
- **Second teach, after the fixes:** shots at +11 to +17 dB, no "loud room" flags. The shots were loose
  (leave-one-out 0.34-0.61, nothing for T2 to flag), so the threshold sat at the 0.50 floor. The test
  match was 0.527. Saved, and restored after a reset.
- **5 more doorbells:** 1 alerted as Doorbell, 2 as Siren, 2 didn't alert. Siren scored 0.53-0.64
  against Doorbell's 0.46-0.55: through this speaker the doorbell clip sits close to the built-in Siren.
- **9 untaught sounds** (clapping, coughing, a 3-tone chime): 5 rejected. Wrong alerts: 2 Doorbell
  (0.51 and 0.55), 1 Water running, 1 Alarm.
- **Reading:** a loose teach gives a 0.50 threshold that lets other sounds in. For the demo, teach
  louder (about 75%, DEMO.md) or with a distinctive real sound such as keys, and redo any shot T2 flags.

**Demo rehearsal at 12:00 PM, Mac volume 44 as found** (`gates/g3_teach_rehearsal.txt`; `g3_play.py teach doorbell`, then `trials doorbell --n 5`):
- **Teach:** 8 of 8 shots taken (+14.3 to +17.9 dB), tight: leave-one-out 0.95-0.97, threshold 0.85
  (the maximum). No "unlike" or "Sounds like" warnings. Saved, replacing the loose 0.50 profile.
- **5 trials:** 4 alerted as Doorbell (0.95-0.96). The 5th was quieter (+10.4 dB) and scored 0.82
  against 0.85, so nothing showed (not a wrong alert). Siren came second at 0.40-0.56, under its 0.59
  threshold: no Siren confusions this time.
- Trials 1-2 were also felt through the table (23 mg): the laptop speaker shakes it.
- **Reading:** this time the clips came out tighter at volume 44 than at 69 earlier, possibly from the laptop's
  position. A tight teach gives a strict 0.85 threshold: a doorbell 10 dB over the room can miss.
  For the demo, play at a steady level, and turn the knob up (thresholds down by as much as 0.10) if
  one misses.

## Built-in calibration on the board (G3 row 7): tried, not kept

At 12:05 PM, with the owner's OK (`gates/g3_calibrate.txt`, `gates/g3_builtin_cal.txt`):
- `g3_play.py calibrate --n 10`: 7 to 12 board examples per built-in from ESC-50 folds 1-4 through
  the Mac speaker. The Mac volume was 38 at the start; the owner changed it during the session (81,
  then 100 for the test).
  - Every measured threshold came out at the 0.50 floor: the clips in a class vary too much for
    the leave-one-out rule.
  - Knock's measured profile was far from the trained one (cosine 0.18). Dog, siren and alarm were
    close (0.83, 0.81 and 0.94).
- Gotcha: adding `assets/ss_board_profiles.h` doesn't rebuild `main.cpp` (the dependency file only
  lists headers that existed). The first flash still had the trained profiles; touch `main.cpp`.
- Test on fold 5, stopped by the owner after 2 of 7 classes (16 clips):
  - Knock 3/8 (1/8 with the trained profiles), Dog 2/8 (6/8 before).
  - 7 of the 16 alerted as the wrong sound (5 of them Water running), because every threshold sat at 0.50.
- **Not kept:** the board is back on the trained profiles (flashed 12:15 PM). The owner watched the
  trained profiles catch sounds well live. The measured header is kept in
  `gates/ss_board_profiles_cal1.h`, out of the build.

## Snooze (A8) and "Someone is talking nearby" (H8)

Built and flashed at 12:20 PM, no sound. Board tests: smoke 16/16, `tools/ss_t7_test.py` 18/18
(`gates/t7_test.txt`). ITCM 97.88% (5.4 KB free).
- **Snooze:** DOWN on an alert (not on a "Maybe") snoozes that sound for 10 min. Its detections still go
  into the timeline (marked read) but raise no alert, banner or lights. A notice says "Alarm snoozed:
  no alerts for 10 min; still in the timeline". Settings > Sounds shows "Snoozed" on that row, and a
  press ends the snooze. Forgetting or re-teaching a taught sound clears its snooze.
  - Tested with injected alarms: DOWN snoozed (screen home); a second alarm was logged ("snoozed:
    logged, no alert") and the screen stayed home; a press on the Alarm row ended it; the next alarm
    alerted.
- **Talking nearby:** while Speech is the best match over its threshold, and for 3 s after, the line over
  the spectrogram turns into a yellow "Someone is talking nearby". It's redrawn only when it changes.
  Rendered in the UI simulator (`demo/screens/29_home_talking.png`); seeing it on the board needs speech
  in the room.

## Full-app smoke test

`tools/ss_smoke.py`, no sound, about 1 minute. It resets the board, then drives every screen and path
with host commands:
- alarm and knock injection; press to dismiss; LEFT marks wrong;
- the boot test clip as a banner; the timeline, with row 0 marked right; stats;
- DOWN opens the menu and LEFT leaves it;
- teaching Keys with 8 silent shots, then cancelling;
- 10 s of forced full redraws.

**Result at 11:20 AM: 16 of 16 pass, 0 audio blocks dropped, 0 IMU errors** (`gates/smoke.txt`).

The first run failed two checks: DOWN didn't open the menu, so the following LEFT went to the
timeline. The firmware was fine. `tools/jlink.py` wrote `ss_arg` and `ss_cmd` in separate J-Link
sessions and ignored a write that failed while the RTT logger held the probe, so the board took
command 8 with the previous argument (0: no key). `Board.command()` (CLI: `jlink.py <elf> cmd C N`)
now writes both in one session, and every write is checked. `g3_play.py` and `c2_power.py` use it.

## Screen tour on the board (1 PM)

`tools/ss_tour.py`, no sound, about 4 minutes. It restarts the app, then shows every screen for a few
seconds and reads the screen back over J-Link at each step (`gates/tour.txt`). The tour visits:
- Ready, then Home.
- Alerts: Alarm, then "still sounding", then snoozed; urgent Glass breaking; the "maybe" Baby crying.
- Knock felt, then heard only; the Dog banner; the taught Doorbell.
- Timeline and Stats.
- Settings, then Sounds, where the snooze is ended.
- Calibrate room; Microphones off; night mode dark.
- The teach name screen, then a Keys teach with T2 and T6, a redo and a cancel.

**Results:**
- All 30 steps reached the expected screen.
- 0 audio blocks dropped and 0 IMU errors over 40 stats lines; IMU times back at most 1.08 ms.
- Night mode logged "screen dark" after 15 s. The owner confirmed by eye at 1:25 PM that the backlight
  goes off; see the next section.

**Finding: a "wrong" mark on an injected alert damages a taught sound.**
- An injected alert (host command 13) carries the profile's own reference embedding. LEFT on it keeps
  that reference as a counter-example (T4).
- For a taught sound the counter-example is saved in the flash. A real match then scores almost the
  same against the counter-example as against the profile, so about half of real matches are vetoed.
- During the tour the board registered LEFT on Alarm, Baby crying and Doorbell, which the script never
  sent. The board felt 140 taps during the tour against 4 after it, so it was being handled.
- The Doorbell mark was saved (store seq 1253). It was removed with the owner's OK (counter-example
  count 0 over J-Link, then host command 11): seq 1254, 824 bytes, as before the tour.
- Checked after a restart: 0 counter-examples for every sound.
- Not fixed in the firmware (feature freeze). The fix would be to keep no counter-example from an
  injected event. There is also no way on the board to undo a wrong mark short of forgetting the sound.

## Hand and eye checks (1:25 PM)

These were the last two items not yet checked. No sound was played (`gates/hand_checks.txt`).

- **Night mode turns the backlight off: pass.** Night mode was turned on with host keys. The owner watched
  the home screen go fully black, with no backlight glow, after 15 s. The board logged "screen dark (night
  mode)" twice.
  - Each time, a real nearby knock lit the screen again: a "maybe Knock" alert, felt at 24 to 31 mg, at
    +15 to +26 dB. Important alerts are meant to wake it.
- **The knob: pass.** The owner turned the blue R79 knob, and a notice with a bar followed it on screen.
  - The board logged 24 `[SS] knob:` lines from 0% to 48%; at 0%, thresholds are +0.100.
  - The knob was left at 50% (+0.000). After a restart the board runs as taught until the knob is turned,
    and a bump from the middle stays close to as taught.
- **State after the checks:** the board restarted on the same store (seq 1254: Doorbell taught, threshold
  0.85, Important; all built-ins on). Night mode is off, there are 0 wrong marks, and Ready took 1278 ms.

## IMU times back with J-Link attached

`times back` (the largest backward step between IMU sample times) was not new: `gates/c3_rtt.log`
already had 44 and 108 ms. Every jump came after a long full-screen draw (alert, teach: 50-125 ms),
and the sensor clock reading spiked at the same time (+20,000 and +42,000 ppm against a steady +6,700).

**Cause:** a J-Link detach clears DEMCR.TRCENA, which stops the DWT cycle counter until
`ss_cycles()` sets it again. The RTT logger and every host command attach and detach. A detach
during a draw stopped the counter for the rest of the draw, so cycle time fell behind real time.

**Fix:** `SysTick_Handler` re-arms the counter every 1 ms, so a detach costs at most 1 ms.

**Test:** 30 s of forced full redraws (host command 14) with the logger running (`gates/imu_back.txt`):

| | times back, max | sensor clock reading |
|---|---|---|
| Before | 31.8 ms | +6,516 to +13,583 ppm |
| After | 0.55 ms | +6,592 to +7,041 ppm |

Without a debugger attached this never happened. Felt-knock matching (±100 ms) was only at risk
when a detach landed between a knock and its conversion to milliseconds.

## Sounds page (T7) and "still sounding" (H5)

Settings > **Sounds** (`tools/ss_t7_test.py`, no sound; 10/10 at 11:40 AM, 14/14 with rename and
room calibration at 11:50 AM; `gates/t7_test.txt`; board screenshot `gates/t7_board_sounds.png`):
- Each built-in that alerts can be turned off: it is then not shown or logged, and doesn't start
  "What was that?". A knock that is off isn't alerted when felt either. Saved in the flash record's
  `builtin_mute` (already in the format, so earlier saves still load).
  - Tested: Dog barking off, then the boot test clip (a dog, 0.79) gave "unknown" and no banner.
    Still off after a reset (`[STORE] ... built-ins off: 0x2`). Back on.
- Each taught sound's urgency cycles Info / Important / Urgent; it's saved and restored with the sound.
  - Tested: Doorbell to Urgent, then an injected Doorbell alert was urgent. Back to Important.
- **Rename:** RIGHT on a taught sound gives it the next preset name that no other taught sound has
  (Keys, Doorbell, Clap, Microwave, Kettle, Phone, Washer, My name). The icon and colour come with the
  name; the urgency set in the menu stays. There's no keyboard, so names come from that list.
  - Tested: Doorbell became Clap, still Clap after a reset, then stepped back to Doorbell.
- LEFT or RIGHT on the Sounds page goes back to Settings, on the Sounds row.
- The Settings menu now holds 16 rows (it was 12; 4 settings + 8 taught + forget all + back was
  already 14, which would have overflowed with 8 taught sounds).

**Calibrate room (S5):** Settings > Calibrate room measures 10 s (500 blocks of 20 ms) and sets the
floor to their median level, so a cough or a click during it doesn't count. The row shows progress,
then the result ("−50 dB"), and the note says how loud a sound must be (floor + 8 dB). The floor keeps
tracking the room afterwards as before. This is useful after a long loud sound has pulled the floor up.
Tested: −50.5 dB in this room, the same as the running floor in the stats lines (−49 to −50). Board
screenshot `gates/s5_board_settings.png`. ITCM 97.69%: putting the new code in MRAM and using `expf`
(already linked) instead of `powf` took the first build's 98.02% down by 864 bytes.

**"Still sounding" (H5):** a sound repeated and heard again within 15 s shows "still sounding" on the
alert ("9 times, for 2 min, still sounding"), on the home list and in the timeline. The screen is
redrawn when that stops being true. Tested: two injected alarms 2 s apart showed it, and it cleared
15 s later. Detail lines now shrink to fit their row.

The first run of the test failed 4 checks: a real sound in the room raised an alert just after a
reset and ate a key press, so the test turned off Water running instead of Dog barking. The test
now reads the menu selection from the board and moves to the row it wants. It also restores the
settings it found (`--set 0 1` puts back all built-ins on and the taught sound at Important).

## C4 details

**Recordings:** [c4_take1.wav](gates/audio/c4_take1.wav) has 16 claps at 1 m: 5 at position A, 6 at the middle position (a short end, about 1 m from the left mic), 5 at position B, plus the start and stop joystick clicks. [c4_take2.wav](gates/audio/c4_take2.wav) has 5 key jingles. Both came out of the PSRAM over J-Link at 64 KB/s. Analysis: `tools/mic_geometry.py`, written to [c4_mic_geometry.json](gates/c4_mic_geometry.json).

**Mic spacing:** 8 cm by ruler.

**Lags** (positive means mic 5 hears it later):
- **Calibration with this Mac's speaker** (`tools/mic_calib.py`: 5 synthetic claps per run, results in [c4_mic_calib.json](gates/c4_mic_calib.json)). The laptop sat on the table in line with the pair, as far out as the cable allowed. Left end **+3.20** (per-clap spread 0.12); right end **−3.61** (spread 0.22). The channel offset, (left + right) / 2, is −0.20 samples (13 µs), so in practice there is none. The maximum lag, (left − right) / 2, is **3.40 samples**, i.e. 73 mm of path difference against 80 mm by ruler. Mic 4 is the left mic, as `docs/HANDOFF.md` says.
- **Hand claps** (take 1): middle position, at a short end about 1 m from the left mic, +2.2; positions A and B −1.0 and −1.7. The claps came from standing height, and the positions weren't exactly in line with or side-on to the pair, so these are angles off the line rather than calibration points. +2.2 corresponds to about 50° off the left end. An earlier note read them as a −1.35-sample channel offset; the speaker runs rule that out.
- **H12 (left/right):** GO. A lag of ±3.4 samples against about ±0.2 of spread at one position gives a usable left / middle / right reading. No per-board calibration is needed beyond the −0.2 offset.
- **Mic layout:** the DK-E8 user guide (Figures 2A/2B) shows one mic on the underside at the middle of each long edge. The measured pair points left–right as seen from the person's usual side, so check which edges those are on this board before writing direction labels into the UI.

**Levels:** with the Mac speaker in line with the pair, mic 5 reads 0 to +1 dB relative to mic 4; its gain register is higher (`0xBB0` against `0x800`, +3.3 dB). On the hand claps it read 4.1 dB below mic 4, so the level difference depends on direction and height. Don't use level for direction; the lag is far more reliable.

**Clipping at about 1 m:** at the current gains, 4 of 16 claps and 6 of 10 key onsets reach full scale. Teaching has to flag clipped examples.

**Keys teach preview** (`tools/teach_preview.py`, float encoder ch128; results in [c4_keys_teach_preview.json](gates/c4_keys_teach_preview.json)):
- A profile built from the first 5 key onsets gets a threshold of 0.807.
- All 10 key onsets score 0.85 to 0.97 and are accepted. The claps (0.16 to 0.36) and the room background (−0.06 to 0.16) are all rejected.
- **Built-in false alerts, before anything is taught:** 7 of 10 key onsets clear the "water" threshold, and 3 of 16 claps clear "glass". G3 should count these.
