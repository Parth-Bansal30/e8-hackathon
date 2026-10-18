# Handoff: continuing Sound Sense (2026-10-18)

**Updated 11:25 AM PDT.** The board's J-Link is now on the laptop with the et-venv (it has torch,
the datasets and the toolchain), so offline work and board work happen on one machine.

Since 10:10 AM:

- Two-stage alert decision: clear built-in matches alert in a median 437 ms (tested live).
- 8 examples per taught sound, with a two-row teach screen.
- T2 complete: "loud room", and "unlike the others" with UP to make that example again.
- T6 "Sounds like …" warning.
- Host command 17: teach shots without sound.
- Docs pass: controls and host commands in README and DEMO.md.
- 11:10-11:25 AM: live 8-example teach. Two bugs found and fixed ("loud room" during the sound, T2
  with two stray shots). Full-app smoke test `tools/ss_smoke.py`: 16/16 pass. `tools/jlink.py cmd`
  sends host commands safely while the RTT logger runs.

Done between 9 and 10:10 AM, without playing any sound:

- C3 rows 1-4 pass.
- A5 urgent alerts escalate.
- A6 night mode.
- T4 marking sounds right or wrong.
- H6 "What was that?"
- S6 microphones off and privacy note.
- T5 sensitivity on the knob.
- T7 forgetting taught sounds.
- The IMU timestamp fix.
- A bug fix: banners and alerts were closing at once.

`gates` was merged into `main` at 9:53 AM ([PR #1](https://github.com/Parth-Bansal30/e8-hackathon/pull/1), merge `f59e2a2`).

Full feature status: [STATUS.md](STATUS.md). The submission session's files are
[HANDOFF-submission.md](HANDOFF-submission.md) and [SUBMISSION.md](SUBMISSION.md).

Work stays on branch **`gates`**. Read in this order:

1. This file.
2. [GATES-results.md](GATES-results.md): what was measured.
3. [HANDOFF-gates.md](HANDOFF-gates.md): test specs C2, C3 and C5.
4. [SOUND-SENSE.md](SOUND-SENSE.md): the feature list, P0 and P1.
5. [HANDOFF.md](HANDOFF.md): toolchain setup.
6. [DEMO.md](DEMO.md): the demo run sheet.

## Deadline

- Devpost closes at **5:00 PM PDT**; the team target is **4:30 PM**.
- The submission session needs the judged tag, so the board session freezes by about **3:45 PM**.
  See "Wrap-up" at the end.

## Rules from the project owner

- Ask before:
  - repartitioning MRAM
  - changing SETOOLS config
  - deleting datasets or caches
  - pushing or merging into `main` (always through a PR the owner has OK'd)
- Commit and push `gates` after each test.
- One MPS training at a time.
- Don't overwrite existing models or `firmware/app`. Reuse `firmware/app/kws.c` instead of copying it.
- Don't commit `firmware/.vscode/` changes or `docs/HANDOFF-gates-dream-canvas.md`.
- If a test runs 1.5x over its time box, stop, record what you have and move on.
- **No sound in the room without asking first.** Never raise the Mac volume. Test silently where
  you can, with the host commands below.

## State

| Area | Status |
|---|---|
| App `firmware/soundsense/` | Release build flashed at 11:10 AM: two-stage decision, 8 examples, T2 (two stray shots, loud room before the sound), T6, host command 17. Smoke test 16/16 |
| Gates | G1 SOFT, G2 SOFT, G3 SOFT (short run), C1 GO, C2 GO, C3 rows 1-4 pass (row 5: backlight on/off only, no dimming), C4 GO, C5 done (12.6%, target 10%) |
| P0 | 16 of 16 done |
| P1 | 11 done, 4 partial (H5, H7, S5, T7), 1 not started (H4 smoke vs CO) |
| Not yet checked by eye or hand | Night mode turning the backlight off; turning the knob (T5) |

## Setting up a new machine

1. Toolchain, SETOOLS and J-Link: as in [HANDOFF.md](HANDOFF.md). Fetch the ExecuTorch libs with
   `tools/fetch_et_bundle.sh`.
2. These are gitignored; fetch them again if needed:
   - `model/esc50/` (ESC-50-master)
   - `model/speech_commands/`
   - `model/ss_cache/`
   - `firmware/third_party/executorch/et_bundle/`

   Torch lives in the et-venv (`~/alif/executorch-alif/et-venv` on the old laptop). It is **not on
   the board laptop**.
3. Build and flash with `tools/build_flash.sh soundsense release`, about 45 s. Add `noflash` to
   build only. Debug builds are -O0 and draw 3x slower.
4. Python tools: run them with the repo venv as `.venv/bin/python tools/...`. The system python3's
   scipy fails on the board laptop with a dyld `__thread_bss` error. To create the venv:
   `/opt/homebrew/bin/python3.12 -m venv .venv && .venv/bin/pip install numpy scipy pillow`.
   On the laptop with the et-venv there's no `.venv`. Use `/opt/anaconda3/bin/python3` (the default
   `python3`) for `tools/`. The et-venv's scipy needs numpy 2 and fails to import (`np.long`); use the
   et-venv only for `model/`.

## How the app works (firmware/soundsense)

**Boot.** The **Ready** screen (S1) runs these checks, then goes home after 2.5 s:

- the mics: level, silent, clipped, left/right mismatch
- the NPU, on a test vector against the host's embedding
- restoring taught sounds from flash
- the IMU and the lights

A failure stays on screen (red LED blink) until a key is pressed.

**`main.cpp`** holds everything at app level.

- **Live view.** Every 100 ms the encoder runs on the NPU (about 1 ms), but only:
  - on the home screen
  - while the level is over the floor + 6 dB, and for 300 ms after (E1)

  Otherwise the top 3 reads "Quiet: NPU resting". The loop sleeps in WFI whenever no audio block
  is waiting (E2).
- **Decisions.** Each sound onset (`ss_audio.c`) is classified once, **0.8 s later**, or **0.4 s
  later** if the best match is a clear alerting built-in (`decide_early()`; not yet tested on the board).
  - `classify()` compares the sound by cosine against the built-in and taught profiles
    (`ss_match.c`).
  - The threshold is moved by the knob offset.
  - The result is vetoed when the sound is closer to a counter-example (T4) than to the profile.
  - A loud sound that matches nothing goes to `what_was_that()` (H6).
  - A knock is "felt" if the IMU (`ss_imu.c`) saw one within ±100 ms.
- **Screens.** READY 0, HOME 1, ALERT 2, TIMELINE 3, STATS 4, TEACH 5, MENU 6 (the `scr` variable).
  Drawing comes from `ui/`. To render every screen on a laptop: `make -C tools/ui_sim`, which
  produces 25 PNGs in `docs/demo/screens/`.
- **Joystick on the board.** The bits are DOWN=1 (A), LEFT=2 (B), RIGHT=4 (C), UP=8 (D), PRESS=16.

  | Screen | UP | DOWN | LEFT | RIGHT | PRESS |
  |---|---|---|---|---|---|
  | Home | Teach (or teach the "What was that?" sound when that banner shows) | Menu | Timeline | Stats | — |
  | Alert | — | — | Wrong sound | Right sound | OK, dismiss |
  | Timeline | Pick row | Pick row | Mark wrong | Mark right | Back |
  | Menu | Move | Move | Home | Home | Choose (forgetting asks twice) |
  | Teach (recording or testing) | Make the flagged example again (T2) | — | Start over | Cancel | Save |

  The menu has these rows:
  - Night mode
  - Microphones
  - Sensitivity (shows the knob value)
  - Privacy
  - Forget each taught sound
  - Forget all
  - Back
- **Taught sounds.** `ss_store.c` keeps two copies of an `SSND` record with CRC32 in the OSPI1
  flash: A at 0x00100000 and B at 0x00110000.
  - A save writes the older copy, one 4 KB sector erase per main-loop pass.
  - Each record holds up to 10 examples (`SS_STORE_MAX_EX`) and the counter-examples.
  - Teaching, forgetting and marking all save.
- **RTT log lines.**

  | Line | What it reports |
  |---|---|
  | `[S1]` | Ready screen and self-test |
  | `[C1]` | NPU check |
  | `[STORE]` | Restore at boot and each save |
  | `[SS] t=... best '...' score (thr) ... -> result` | Each decision |
| `[SS] t=... early, N ms after the onset` | The next decision line was made at 0.4 s |
  | `[TEACH]`, `[CAL]` | Teaching and re-measuring built-ins. `[TEACH] T2: shot N unlike the others` and `[TEACH] T6: sounds like '...'` are the checks |
  | `[C3] alert on screen N us after the decision` | Alert timing |
  | `[SS] knob:` | Knob turned |
  | `[SS] screen dark (night mode)` | Night mode turned the screen off |
  | `[SS] stats` (every 10 s) | CPU active %, NPU busy, dropped blocks, IMU `times back` and clock ppm, saves |
  | `[SS] draw` | Draw timing |

### Host control over J-Link (silent testing)

To send a command: `tools/jlink.py <elf> cmd C N` (one J-Link session, so the board never takes C
with a stale `ss_arg`; separate `set` calls can lose the argument while the RTT logger runs). Python scripts use
`g3_play.command(C, N)`.

| C | What it does |
|---|---|
| 1 | Teach preset N: starts recording, so it needs real sound. Presets: 0 Keys, 1 Doorbell, 2 Clap, 3 Microwave, ... |
| 2 | Save the taught sound |
| 3 | Home, or cancel |
| 4 | Forget all taught sounds |
| 5 | Stats screen |
| 6 | Timeline screen |
| 7 | Set the clock: N = ms after midnight |
| 8 | Press keys: N = the joystick bits above |
| 9 | Re-measure built-in N |
| 10 | Finish |
| 11 | N back-to-back flash saves (capped at 1000) |
| 12 | Fill the record with 8 stand-in taught sounds (thr 0.99, never match) |
| 13 | **Fake a detection** of profile N & 0xff. Flags: 0x100 maybe, 0x200 felt, 0x400 heard only |
| 14 | **Full redraws 10 times a second** for N seconds (stress test) |
| 15 | **Run the boot test clip** (a dog bark) through the real `classify()` path |
| 16 | **Mark timeline row** N & 0xff wrong, or right with 0x100 |
| 17 | **A teach shot without sound** (teach screen only): N = 0 the boot test clip, N = 1 the room's last 1 s (an outlier). Seven 0s and a 1 trigger T2 on shot 8 and T6 "Sounds like Dog barking" |

Profile indices: knock 0, dog 1, baby 2, alarm 3, glass 4, siren 5, water 6, speech 7,
background 8. Taught sounds start at 9.

To check the result, read variables over J-Link:

| Symbol | What it is |
|---|---|
| `_ZL3scr` | Current screen |
| `_ZL9banner_on` | Banner showing |
| `_ZL5night` | Night mode |
| `_ZL7mic_off` | Microphones off |
| `_ZL8sens_pct` | Knob position |
| `_ZL8sens_off` | Threshold offset from the knob |
| `_ZL12seed_pending` | "What was that?" sound waiting to seed teaching |

## Tools

| Tool | Use |
|---|---|
| `tools/rtt_log.sh soundsense release <log>` | Follow the RTT log into a file. Start it before any script that reads the log. |
| `tools/build_flash.sh <project> [debug\|release] [noflash]` | Build (and flash). The log goes to `/tmp/build_<project>.log`; read it when cbuild's error is vague ("read .../firmware: is a directory" was an ITCM overflow). |
| `tools/jlink.py <elf> get\|set\|cmd\|rtt\|reset` | Read and write variables, send a host command, follow RTT, reset |
| `tools/ss_smoke.py` | Full-app smoke test over J-Link, no sound: resets the board, 16 checks, about 1 minute |
| `tools/c3_ui.py [log]` | C3 rows 2-4 without sound: fake detections plus the board's own timing |
| `tools/pc_profile.py [elf] [samples]` | Statistical CPU profile (DWT PC sampling). WFI sleep shows inside `main`. |
| `tools/ss_screen.py` | Screenshot of the frame buffer over J-Link (12 s; tears during redraws) |
| `tools/g3_play.py` | G3 with the Mac speaker: `builtin`, `teach`, `trials`, `reject`, `noise`, `calibrate`. **Needs sound.** |
| `tools/demo_sound.py` | One demo sound per command. **Needs sound.** |
| `tools/c2_power.py` | Flash bench with power pulls |
| `tools/g2_report.py`, `tools/g2_raw.py`, `tools/mic_calib.py`, `tools/mic_geometry.py`, `tools/teach_preview.py` | Earlier gates |

## Gotchas learned the hard way

- **Don't rebuild while a logger or test runs.** A new ELF moves `_SEGGER_RTT`, and the logger
  reads garbage. Stop it first: `pkill -f rtt_log.sh; pkill -f 'jlink.py.*rtt'`.
- **`g3_play.command()` retries.** It can send a key press twice, or say "board did not take
  command" when the board did take it. Wrap it in try/except and check the result by reading
  `_ZL3scr` and similar, not by trusting the return.
- **Time differences use `since(t, now)`**, never `now - t`. The loop reads `now` once at the top,
  and a decision stamps its times after a >1 ms inference, so `now - t` wrapped and closed banners
  at once (fixed 10-18).
- **ITCM is 97.0% full** (254,224 of 256 KB; all `.text` lands there). New boot-only or rare code
  goes to MRAM:
  - mark the function `SS_COLD` (`ss_board.h`) or `UI_COLD` (`ui/ui.c`), or
  - add an object pattern to the MRAM section of the shared linker script
    `firmware/device/ensemble/RTE/Device/AE822FA0E5597LS0_M55_HP/linker_gnu_mram.ld.src`.
    The OSPI and ADC drivers and `ss_store.c` are there already.
- **IMU frame times come from the BMI323 sensor clock**, anchored when a read drains the FIFO.
  The sensor runs 0.68% fast, so cycles per tick are re-measured every 5 s. `[SS] stats` prints
  `times back` (should stay at 0). Setting `ss_imu_old_timing` = 1 brings the old timing back for
  comparison.
- **The knob only applies after it's turned.** The ADC can read 0% at boot. The board starts as
  taught, wherever the knob was left.
- **LPGPIO reads are slow** (~130 µs each). The joystick reads the whole port once every 10 ms.
  The IMU poll busy-waits on I3C, so it also runs every 10 ms.
- **The joystick switches bounce.** The app has a 250 ms lock-out and ignores sounds for 0.8 s
  after a press.
- **The mics surge at start-up.** The S1 mic check waits 200 ms before measuring.
- **The laptop on the same table makes knocks "felt".** Lift it to test heard-only knocks.
- **The Mac was left muted.** Unmute it (at the current volume, never louder) only when the owner
  OKs a sound test.

## Next steps, in priority order (as of 11:10 AM)

### Silent: can be built and mostly tested without sound

1. **Done at 11 AM: 8 examples, T2, T6** (`model/shots_eval.py`; results in
   [GATES-results.md](GATES-results.md#teaching-8-examples-t2-t6)).
   - Tested on the board with host command 17: T2 flagged the outlier, UP replaced it, T6 warned.
   - Still to do: SUBMISSION.md and VIDEO-SCRIPT.md say "5 times" and "5 examples"; tell the
     submission session.
   - Tested live at 11:10 AM with the doorbell clip at volume 69; see
     [GATES-results.md](GATES-results.md#teaching-8-examples-t2-t6). Two bugs fixed. The
     second teach was loose (threshold 0.50, Siren confusions), so the demo should teach louder or
     use keys.
2. **Done: T2** (see 1).
3. **Done: T6** (see 1).
4. **Checks by eye and hand, no sound.** Ask the owner to look or turn:
   - Night mode on, then wait 15 s on the home screen: the backlight should go off.
   - Turn the blue R79 knob: a notice with a bar should show, and an `[SS] knob:` log line should
     appear.
5. **Done: docs pass.** The controls (menu, marking, teach keys) and host commands 13-17 are in
   README and DEMO.md.
6. **Smaller P1 gaps, if there's time:**
   - H5 "still sounding"
   - S5 explicit 10 s calibration
   - T7 rename, urgency, and built-ins on/off
   - C3 row 5 PWM dimming: needs the RTE UTIMER4 driver and masking its per-cycle IRQs
   - H4 smoke vs CO (a bigger job)

### Needs sound in the room (ask the owner first; current volume only)

7. **Built-in accuracy (H1).**
   1. Run `g3_play.py calibrate --n 10`. Check that each class gets about 10 examples in the
      `[CAL]` lines.
   2. Rerun `g3_play.py builtin`.

   This writes `assets/ss_board_profiles.h`, which the firmware picks up at build time. Compare
   against the 54% run (`docs/gates/g3_builtin_trained.txt`).
8. **G3 rows not measured:**
   - taught: `teach doorbell`, then `trials doorbell --n 10`
   - rejection: `reject`
   - false alarms: `noise --minutes 10`
9. **G2:** re-measure knock alignment after the IMU timestamp fix (real knocks on the table).
10. **C5:** CPU load during continuous speech.

### Not on this laptop

11. **Latency.** Offline test done at about 10:35 AM (`model/delay_sweep.py`). Results:
    [GATES-results.md](GATES-results.md#alert-delay-offline).
    - Don't just shorten `DECIDE_DELAY`: at 0.45 s the built-ins lose about 9 points.
    - Firmware for a two-stage decision is written (`decide_early()` in `main.cpp`). An early check at
      0.4 s decides clear built-in matches (threshold + 0.05); everything else waits for 0.8 s as
      today. Offline that alerts 55% of built-in sounds at about 0.45 s.
    - Flashed at 10:40 AM from the laptop with the et-venv (the board's J-Link is on it now). The
      Ready screen passes and `[C1]` is unchanged; weak matches wait for 0.8 s as they should
      (`docs/gates/early_rtt.log`).
    - **Tested with sound at 10:50 AM** (`g3_play.py builtin`, `docs/gates/g3_builtin_early.txt`).
      Recognition is unchanged (30/56, 54%). 18 of 30 recognised sounds alerted early, median 437 ms
      from onset to screen, 0 wrong. Knocks fell 5 → 1 because none were felt this time.
    - The IMU `times back` read up to 3 ms after this flash (it was 0 in `c3_rtt.log`). It isn't from
      the early path; flash the previous build to see whether it's new.
    - Confirmed, so the 0.85 s claims need updating (not done yet; ask the owner): README (lines 68, 82, 192), DEMO.md "Alert
      timing", SUBMISSION.md "Alert delay", HANDOFF-submission.md. Tell the submission session.

## Wrap-up (board session, by about 3:45 PM)

Agreed with the owner at about 10 AM; also in SUBMISSION.md.

1. Stop feature work. Flash the release build from the final commit and run the Ready screen once.
2. Update STATUS.md and GATES-results.md, then commit and push `gates`.
3. Tag the commit the board is flashed from: `git tag v1.0-judged <hash> && git push origin v1.0-judged`.
4. Open a PR `gates` → `main` and merge it with a merge commit, once the owner OKs it. Keep the
   `gates` branch.
5. Give the submission session the hash. It replaces `<hash>` in SUBMISSION.md section 1 and in
   "About the project".
