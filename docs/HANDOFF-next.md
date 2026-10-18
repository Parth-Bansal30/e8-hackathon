# Handoff: continuing Sound Sense on another machine (2026-10-18)

**Updated ~9 AM on the board laptop (M1).** Done here since the first version: C2 flash save (GO),
S1 Ready screen, C5 duty cycle (CPU active 12.6% quiet), new tools, README rewrite. Full feature
status: [STATUS.md](STATUS.md). Submission brief for a separate session:
[HANDOFF-submission.md](HANDOFF-submission.md).

Branch **`gates`** (pushed; never push to `main`). Read in this order: this file,
[GATES-results.md](GATES-results.md) (what was measured), [HANDOFF-gates.md](HANDOFF-gates.md)
(test specs C2/C3/C5), [SOUND-SENSE.md](SOUND-SENSE.md) (feature list, P0/P1),
[HANDOFF.md](HANDOFF.md) (toolchain setup), [DEMO.md](DEMO.md) (demo run sheet).

## Rules from the project owner

- Ask before: repartitioning MRAM, changing SETOOLS config, deleting datasets or caches, pushing to `main`.
- Commit and push `gates` after each test. One MPS training at a time. Don't overwrite existing
  models or `firmware/app`; reuse `firmware/app/kws.c` instead of copying it.
- If a test runs 1.5x over its time box, stop, record what you have, move on.

## State

| Area | Status |
|---|---|
| App `firmware/soundsense/` | Runs on the board (release build flashed). Listen, match, teach (5 examples), alerts, LEDs, timeline, stats, knock felt vs heard |
| Gates | G1 SOFT, G2 SOFT, G3 SOFT (short run), C1 GO, C2 GO, C4 GO, C5 done (12.6%, target 10%), C3 LEDs only (rows 2-5 pending) |
| P0 features | About 15 of 16 done. Done since: S2 flash save (C2), S1 "Ready" self-test screen, S4 duty cycle (C5). Partial: H1 accuracy (54% from a laptop speaker), T2 checks |

## Setting up the new machine

1. Toolchain, SETOOLS, J-Link: as in [HANDOFF.md](HANDOFF.md). `tools/fetch_et_bundle.sh` for the ExecuTorch libs.
2. Not in git (gitignored), fetch again if needed: `model/esc50/` (ESC-50-master), `model/speech_commands/`,
   `model/ss_cache/`, `firmware/third_party/executorch/et_bundle/`. Torch lives in the et-venv
   (`~/alif/executorch-alif/et-venv` on the old laptop); the system python3 only needs numpy, scipy, Pillow.
3. Build and flash: `tools/build_flash.sh soundsense release` (about 45 s). Debug is -O0 and 3x slower to draw.

4. Python tools: if the system python3's scipy fails to load (it does on the board laptop's macOS:
   a dyld `__thread_bss` error), use a venv: `/opt/homebrew/bin/python3.12 -m venv .venv &&
   .venv/bin/pip install numpy scipy pillow`, then run tools as `.venv/bin/python tools/...`.

The two helpers that lived in the old laptop's temp folder are now in git:
`tools/build_flash.sh soundsense release noflash` (build only) and `tools/rtt_log.sh` (follow the
log into `docs/gates/ss_rtt.log`).

## How the app works (firmware/soundsense)

- Boot: the **Ready** screen (S1) checks the mics (level, silent, clipped, left/right mismatch), runs the
  NPU on a test vector against the host's embedding, restores taught sounds from flash, checks the IMU
  and lights, then goes home after 2.5 s. A failure stays on screen (red LED blink) until a key.
- `main.cpp`: everything app-level. Every 100 ms the live view runs the encoder (NPU, about 1 ms), but
  only on the home screen while the level is over the floor + 6 dB and 300 ms after (E1); otherwise
  the top 3 reads "Quiet: NPU resting". The loop sleeps in WFI whenever no audio block waits (E2). Each
  sound onset (`ss_audio.c`) is classified once, **0.8 s later** (onset 0.2 s into the 1 s window),
  by cosine against built-in and taught profiles (`ss_match.c`; same rule as `model/ss_common.py`).
  A knock is "felt" if the IMU (`ss_imu.c`) saw one within ±100 ms.
- Screens and LEDs come from `ui/` (render all screens on a laptop: `make -C tools/ui_sim`).
- Host control over J-Link: `tools/jlink.py <elf> set ss_arg N` then `set ss_cmd C`. C: 1 teach preset N
  (0 Keys, 1 Doorbell, 2 Clap, 3 Microwave, ...), 2 save, 3 home, 4 forget taught, 5 stats, 6 timeline,
  7 clock (N = ms after midnight), 8 press keys (N = SS_JOY_* bits), 9 re-measure built-in N, 10 finish,
  11 N back-to-back flash saves (capped at 1000), 12 fill the record with 8 stand-in sounds (thr 0.99, never match).
- Taught sounds: `ss_store.c` keeps two copies (A 0x00100000, B 0x00110000) of an `SSND` record with CRC32 in the
  OSPI1 flash. A save writes the older copy one 4 KB sector erase per main-loop pass (longest step 124 ms,
  inside the 320 ms audio ring), then programs and reads back. Teach-save and "forget" both save.
  Boot loads the valid copy with the highest sequence number.
- RTT log lines: `[S1]` ready/self-test, `[C1]` NPU check, `[STORE]` restore and each save, `[SS] t=... best '...'
  score (thr) ... felt ... -> result`, `[TEACH]`, `[CAL]`, and `[SS] stats` every 10 s (`active` = CPU awake %,
  passes/sleeps per second, `npu busy`, dropped blocks, IMU samples/errors, saves).

## Tools

- `tools/g3_play.py`: G3 with the Mac speaker (`builtin`, `teach`, `trials`, `reject`, `noise`, `calibrate`). Reads the RTT log file, so the logger must run.
- `tools/demo_sound.py`: one demo sound per command (`alarm`, `doorbell --times 5`, ...).
- `tools/ss_screen.py`: screenshot of the board's frame buffer over J-Link (12 s; tears while the screen redraws).
- `tools/g2_report.py`, `tools/g2_raw.py`, `tools/mic_calib.py`, `tools/mic_geometry.py`, `tools/teach_preview.py`: earlier gates.
- `tools/rtt_log.sh [project] [debug|release] [log]`: follow the RTT log into a file (default `docs/gates/ss_rtt.log`).
- `tools/build_flash.sh <project> [debug|release] [noflash]`: the build log goes to `/tmp/build_<project>.log`
  (read it when cbuild's error is vague; "read .../firmware: is a directory" was really an ITCM overflow).
- `tools/jlink.py <elf> reset`: reset and run without a power pull.
- `tools/c2_power.py`: C2 power-pull and save-cycle bench (watches the log for boots and `[STORE]` lines).
- `tools/pc_profile.py [elf] [samples]`: statistical CPU profile over J-Link (DWT PC sampling, mapped with nm).
  It found the 130 µs joystick reads. WFI sleep shows up inside `main`.

## Gotchas learned the hard way

- **Don't rebuild while a logger or test runs:** a new ELF moves `_SEGGER_RTT`, and the logger reads garbage.
- The RTT logger and J-Link commands share the probe; calls fail now and then. `g3_play.command()` retries.
- The Mac was left **muted**: unmute before any speaker test or demo.
- The laptop on the same table makes laptop knocks "felt"; lift it for heard-only.
- Joystick switches bounce: the app has a 250 ms lock-out and ignores sounds for 0.8 s after a press.
- IMU frame times now come from the BMI323 sensor clock (fixed 10-18): the old "newest frame = read time" timing jumped back up to 69 ms after stalls. `[SS] stats` prints `times back` (should stay 0) and the sensor clock offset (+0.68%). `ss_imu_old_timing` = 1 over J-Link brings the old timing back for comparison.
- **ITCM is 97.6% full** (all `.text` lands there). New boot-only or rare code goes to MRAM: mark the function
  `SS_COLD` (`ss_board.h`) or `UI_COLD` (`ui/ui.c`), or add an object pattern to the MRAM section of the shared
  `firmware/device/ensemble/RTE/Device/AE822FA0E5597LS0_M55_HP/linker_gnu_mram.ld.src` (the OSPI driver and
  `ss_store.c` are there already).
- **LPGPIO reads are slow** (~130 µs each, low-power domain). The joystick reads the whole port once, every 10 ms.
  Don't poll LPGPIO pins in the hot loop.
- **The IMU poll busy-waits on I3C**: it runs every 10 ms (the FIFO holds plenty), not every pass.
- The mics surge at start-up: the S1 mic check waits 200 ms before measuring.
- Don't play test sounds in the board laptop's room without asking, and never raise the Mac volume.

## Next steps, in priority order

1. ~~C2 / S2: save taught sounds in flash.~~ Done (GO, `3043843`, `82367a8`).
2. **Built-in accuracy (H1).** Needs sound in the room. On-board re-measuring (`g3_play.py calibrate --n 10`) failed only because
   the Mac was muted halfway; rerun it with sound on, check each class gets about 10 examples in
   `[CAL]` lines, then rerun `g3_play.py builtin`. It writes `assets/ss_board_profiles.h`, which the
   firmware picks up at build time. Compare against the trained-profile run (54%, `docs/gates/g3_builtin_trained.txt`).
3. **G3 rows not measured:** taught (`teach doorbell`, `trials doorbell --n 10`), rejection (`reject`), false alarms (`noise --minutes 10`).
4. **G1 SOFT says 8 examples per taught sound:** the teach screen only fits 5 circles (84 px each); make them smaller first.
5. **Latency:** the 0.8 s decision delay misses the 0.5 s target. Test 0.4-0.5 s offline with the et-venv encoder before changing `DECIDE_DELAY`.
6. T2 checks (show "unlike the others" from the leave-one-out scores, "background too loud"), C3 rows 2-5,
   the IMU timestamp fix, then P1: A5 (urgent alerts repeat), H6 ("What was that?"), T4, T7, A6.
   Done: S1 "Ready" screen, C5 duty cycle with E1/E2 (`c6bf638`).
