# Handoff: go/no-go tests for Sound Sense

For the Claude session on the **reference Mac** (the M2 with the DK-E8 attached, `et-venv`, the
Speech Commands dataset and `kws_cache/`).
- **What it covers:** the tests that decide Sound Sense's scope, the order to run them in, what
  to measure and how to report.
- **The product:** [SOUND-SENSE.md](SOUND-SENSE.md). Feature IDs such as H2 or F1 refer to its
  catalogue.
- **Hardware facts:** [HARDWARE.md](HARDWARE.md).
- **Setup and board quirks:** [HANDOFF.md](HANDOFF.md), [HANDOFF-memory.md](HANDOFF-memory.md),
  [bringup-notes.md](bringup-notes.md).

Snapshot: 2026-10-18, written on the second laptop (no board, no Python export environment).
Submission deadline: **today, 4:30 PM PDT**.

---

## 0. Read this first

### How these tests work

There are two kinds:

- **Gates (G1–G3)** settle the real unknowns, and their results set the scope:
  - G1: does the encoder learn new sounds from 5 examples?
  - G2: can the IMU feel a knock?
  - G3: does it all work live with our mics in a real room?
- **Build checks (C1–C5)** are lower risk. They verify parts of the product while it's being built.

**Every test builds a piece of the product, not throwaway code.** The encoder, the IMU driver, the
flash store, the recorder and the `soundsense` app skeleton written for these tests become the app.
After the gates, the remaining work is the P0/P1 features in
[SOUND-SENSE.md §3](SOUND-SENSE.md#3-priority-list-build-order).

### Rules

| Rule | Detail |
|---|---|
| Branch | `git fetch origin && git switch -c gates origin/main`. Commit after each test and push the branch (`git push -u origin gates`). Don't push to `main` |
| Keep what works | Don't overwrite `kws_model.pth`, `kws_ethos_u85.pte`, the `mnist*` files, or `firmware/app`. New models get new names (`ss_*`) |
| Reuse the audio front end | `firmware/app/kws.c` is bit-exact with `model/kws_common.py`. Use it from `soundsense` (add `../app` to `add-path`) rather than copying it, the same way the runner is used from `../mnist_executorch` |
| Exporters write into the new project | `export_kws.py` always writes into `firmware/app`. The new exporter writes into `firmware/soundsense/` |
| Leave the memory map alone | Don't repartition MRAM, edit `app_mem_regions.h` or `.alif/M55_HP_cfg.json`, or change SETOOLS settings. If an image won't fit in the 2 MB partition, stop and report |
| One OSPI driver per project | The pack's flash driver and the vendored PSRAM driver (`firmware/psram_test/vendor`) can't be linked together. `soundsense` uses the flash; `recorder` uses the PSRAM |
| One GPU job at a time | The Mac has 8 GB. Train with `nohup … > log 2>&1 &` and do board work meanwhile |
| Timing | Use the `cycles()` helper from `firmware/flash_test/main.c`. It sets `TRCENA`/`CYCCNTENA` again on every read, because each `rtt_dump.sh` detach clears them |
| Debug and release | Time both: `tools/build_flash.sh <proj> release`. The ELF is then at `firmware/out/<proj>/E8-HP/release/<proj>.elf` |
| New firmware projects | Add them to `firmware/alif.csolution.yml`. A first build failing with "is a directory" is normal: `rm -rf firmware/tmp firmware/out/<proj>` and run it again |
| If the board hangs | Flash `hello_rtt` to get the debugger back. For RECOVERY MODE use `./maintenance` → ROM → MRAM Recovery (about 130 s, don't interrupt). See [HANDOFF-memory.md §6](HANDOFF-memory.md#6-if-something-goes-wrong) |
| Honesty | Report numbers as measured; mark anything estimated. A NO-GO with numbers is a useful result |
| Time boxes | At 1.5x a test's budget, stop, write down what's known, move on |
| Ask the user first | Before repartitioning MRAM, changing SETOOLS config, deleting datasets or caches, or pushing to `main` |

### Order (about 4.5 hours to the decision checkpoint; training overlaps board work)

| # | Step | Where | Budget |
|---|---|---|---|
| 1 | Pull, create the branch, start the ESC-50 download (§1.1) | host | 5 min |
| 2 | **G1**: prepare the data and start encoder training in the background | host (MPS) | 45 min hands-on |
| 3 | **C4**: recorder firmware and mic geometry | board | 40 min |
| 4 | **G2**: fast IMU and felt vs heard | board | 75 min |
| 5 | **G1** evaluation once training ends, then export | host | 30 min |
| 6 | **C1**: encoder on the NPU | board | 30 min |
| 7 | **G3**: live test with the `soundsense` skeleton | board | 75 min |
| 8 | **Checkpoint:** write the G1–G3 rows of `docs/GATES-results.md`, push, and tell the user | | 10 min |
| 9 | **C2, C3, C5** while building P0 | board | 45 + 45 + 30 min |

If the user says the second laptop is taking G1, skip steps 2 and 5. Its outputs will arrive on
the `gates` branch.

---

## G1. Does the encoder learn new sounds from 5 examples? (host)

**Decides:** H2 (teaching), H3 (rejecting unknowns), H1 (built-ins), T4 and T6. This is the
project's core risk.

### 1.1 Data

```bash
cd ~/alif/e8-hackathon/model && mkdir -p esc50
curl -fL https://github.com/karoldvl/ESC-50/archive/master.zip -o esc50/esc50.zip && unzip -q esc50/esc50.zip -d esc50
# about 600 MB: esc50/ESC-50-master/audio/*.wav (44.1 kHz, 5 s), labels in esc50/ESC-50-master/meta/esc50.csv
# add model/esc50/ and model/ss_cache/ to .gitignore
```

**Preparing the clips:**
- Resample 44.1 → 16 kHz (`scipy.signal.resample_poly(x, 160, 441)`; `pip install scipy` if it's
  missing).
- **Training:** cut random 1 s windows whose RMS is at least half that of the clip's loudest
  1 s window, so the window contains the event.
- **Evaluation:** use the loudest 1 s window.

**Features:** exactly `kws_common.Features` (49x40 log-mel), so the board's C code works unchanged.

**Class sets** (ESC-50 category names):

| Set | Classes | Used for |
|---|---|---|
| Built-in profiles | `dog`, `crying_baby`, `door_wood_knock`, `clock_alarm` (as "alarm beeping"), `glass_breaking`, `siren`, running water (`pouring_water` + crops of Speech Commands' `_background_noise_/running_tap.wav`) | Training (folds 1–4), testing built-ins (fold 5) |
| Speech and background | Speech Commands words; `_background_noise_` crops | Extra training classes; "speech" and "background" profiles |
| **Held out (never trained)** | `church_bells`, `clock_tick`, `can_opening`, `mouse_click`, `keyboard_typing`, `door_wood_creaks`, `vacuum_cleaner`, `washing_machine`, `footsteps`, `sneezing` | The few-shot and unknown-rejection tests: stand-ins for sounds users will teach |
| Other ESC-50 classes | the remaining 33 | Training only (folds 1–4) |

### 1.2 Encoder and training

| Item | Spec |
|---|---|
| Input / output | int8 `[1, 1, 49, 40]` → int8 `[1, 64]` embedding. The board's runner reads one output, and 64 values are all it needs |
| Body | DS-CNN like `kws_common.DSCNN`, 96–128 channels, 5 depthwise-separable blocks, global average pool, Linear → 64 (no activation on the last layer). Fold BatchNorm before export |
| Budget | ≤ 300k parameters, ≤ 50 M MACs per call |
| Training head (not exported) | A cosine classifier over all training classes: the 40 ESC-50 classes, the 35 Speech Commands words and background. Normalise the embedding and the weights, scale 16, cross-entropy |
| Sampling | Oversample ESC-50 about 8x so environmental sounds aren't drowned out by speech |
| Augmentation | Background noise at 0–20 dB SNR (p = 0.8), gain 0.3–2, ±100 ms shift, synthetic reverberation: exponentially decaying noise, RT60 0.1–0.6 s (p = 0.5) |
| Optimiser | Adam, lr 3e-3 with cosine decay, about 30 epochs. Measure time per epoch first |
| Run | `nohup python -u train_ss.py > out/train_ss.log 2>&1 &` |

**Built-in profiles:** for each built-in class, take the mean of the normalised fold 1–4
embeddings, plus a default threshold (§1.3 rule). Write them to
`firmware/soundsense/assets/ss_builtin_profiles.h`.

### 1.3 Evaluation (float and int8)

For int8, quantise with the PT2E flow from `export_kws.py`. Matching works like this:
- A profile is the mean of normalised embeddings.
- A sound matches the profile with the highest cosine similarity, but only if it clears that
  profile's threshold; otherwise it's "unknown".
- **Threshold rule:** compute each teaching example's similarity to the mean of the others. The
  threshold is the lowest of those minus 0.05, clamped to [0.5, 0.85].

| # | Measurement |
|---|---|
| a | **Few-shot:** 600 episodes of 5-way 5-shot on the held-out classes: 5 teaching clips and 15 test clips per class. Also report 10-way 5-shot |
| b | **Unknown rejection:** each episode teaches 5 held-out classes (5 examples each) alongside the built-in profiles. Test clips: 15 per taught class, 15 from each of the 5 *untaught* held-out classes, 50 background, 50 speech. Report: % of taught sounds accepted as the right class, % of untaught sounds rejected, AUROC of the best-match score for taught vs untaught |
| c | **Built-ins:** fold 5 clips of the built-in classes, plus 100 clips of other non-held-out classes as negatives. Report: % recognised, % of negatives falsely accepted |
| d | int8 vs float for a–c |
| e | Parameters, MACs, .pte size, and whether every op runs on the NPU (the lowered graph must be a single `executorch_call_delegate`) |

Write `docs/gates/g1_metrics.json`, plus a confusion image if useful (under 300 KB).

**Outputs used by the product:**
- `model/ss_common.py`, `train_ss.py`, `export_ss.py`
- `ss_encoder.pth`, `ss_encoder.pte`, `ss_encoder_io.h`
- `firmware/soundsense/assets/ss_builtin_profiles.h`
- `firmware/soundsense/assets/ss_testvec.h`: one clip with its expected int8 features and
  embedding, for the boot self-test, as `kws_testclip.h` does.

| Result | Criteria | Then |
|---|---|---|
| **GO** | Few-shot int8 ≥ 85%. ≥ 90% of taught sounds accepted while ≥ 80% of untaught sounds are rejected. Built-ins ≥ 85% recognised with ≤ 10% false accepts. int8 within 2 points of float | Proceed |
| **SOFT** | Few-shot 75–85%, or rejection 65–80% | Ask for 8 examples per taught sound, keep demo sounds distinctive (chimes, beeps), and promote T4 (fix mistakes) to P0 |
| **NO-GO** | Few-shot below 75% | Try in order, about 1.5 h total: (1) a wider encoder and longer training; (2) more data (an UrbanSound8K or FSD50K subset); (3) distilling from PANNs, which has PyTorch AudioSet weights. If still failing, report: the project needs rethinking |

---

## G2. Can the IMU feel a knock? (board)

**Decides:** F1 (felt vs heard, the fusion headline), F2, F4, F5. Without it, the multimodal score
drops by about 3 points.

**Driver.**
- **What the pack does:** `~/.cache/arm/packs/AlifSemiconductor/Ensemble/2.0.4/components/Source/BMI323_IMU.c`
  writes `ACC_CONF` (register 0x20) = `0x4037`: 50 Hz output (bits 3:0 = 0x7), ±16 g (bits
  6:4 = 0x3), normal mode (bits 14:12 = 0x4). It reads one sample per call, and its register
  helpers are `static`.
- **Make a patchable copy:** copy the file to `firmware/soundsense/vendor/`. Remove the
  `BSP:External peripherals:BMI323` component from any project that uses the copy (otherwise the
  symbols are duplicated), and keep `Device:SOC Peripherals:I3C`.
- **Patch it:**
  - Output rate 800 Hz (0xB) or 1.6 kHz (0xC).
  - Range ±4 g (0x1), for more resolution on light knocks.
  - High-performance mode, if the datasheet requires it at that rate.
  - The scaling: the driver hard-codes 2048 LSB/g, which is right for ±16 g; ±4 g is 8192.
  - **Check every code against the BMI323 datasheet.**
- **Reading samples:** start by polling at the output rate. If that costs more than 10% CPU or
  misses samples, switch to the BMI323 FIFO with a burst read every 20 ms (registers per the
  datasheet).

**Bench.**
- **Project:** `firmware/imu_bench/`, using `../soundsense/vendor/BMI323_IMU.c` and `../app/kws.c`
  for audio.
- **Knock detector:** high-pass the acceleration magnitude, then flag a knock when it exceeds
  6x the running noise level, with an 80 ms refractory period.
- **Audio onset:** take it from the raw PCM. Record the cycle count when each PDM block
  completes, then find the first sample in the block above 6x the noise floor. That gives
  millisecond-level timing; the 20 ms frame level is too coarse.

| # | Measurement |
|---|---|
| 1 | Achieved sample rate and missed samples over 60 s; I3C time per read; CPU % |
| 2 | Felt: 20 knocks on the table at 20, 50 and 100 cm from the board, giving detection rate per distance. If a door is available, also tape the board to it and knock |
| 3 | Alignment: audio onset minus IMU onset for those knocks (median and spread) |
| 4 | Heard only: 20 knock sounds from a phone held 30 cm above the table. Count IMU triggers. Also try the phone lying on the table and note the result: it will probably be "felt", which matters for the demo |
| 5 | Other "felt" events per minute: typing on the same table, putting a cup down, walking past. These are real vibrations; the audio class decides what they are |

| Result | Criteria | Then |
|---|---|---|
| **GO** | ≥ 800 Hz with no missed samples, ≥ 95% felt at 50 cm, heard-only felt ≤ 10%, ≥ 95% of knocks aligned within ±10 ms | F1 becomes P1 item 2 |
| **SOFT** | Works at 20 cm but not at 50 cm | F1 with the "place me on the table by the door" guidance (S8) |
| **NO-GO** | No reliable fast sampling, or knocks not felt | Drop F1, F2, F4 and F5. Fusion falls back to F3 (moved detection) |

---

## G3. Does it work live, with our mics, in a real room? (board)

**Decides:** whether P0 works as designed (H1, H2, H3, T1–T3, H6, S5). This is the real go/no-go.

**Build the skeleton `firmware/soundsense/`** (the start of the product):
- **Template:** `firmware/app/app.cproject.yml`. Files: a new `main.cpp`, `../app/kws.c`,
  `../mnist_executorch/executorch_runner.cpp`, and `ss_match.c` (profiles, thresholds, teaching).
- **Listening loop:** every 100 ms: `kws_make_input`, then the encoder, normalise, match, and
  smooth over 3 runs as `firmware/app` does. Show the alert as text on screen and log
  `[SS] t=… 'dog' cos=0.81 thr=0.70` to RTT.
- **Minimal teaching:** joystick UP starts teaching the next custom slot.
  - Capture 5 onsets (level above the noise floor + 10 dB), each as the 1 s window with the onset
    about 0.2 s in.
  - Show each example's quality: dB above the floor, clipped or not, similarity to the others.
  - Keep the profile in RAM; flash storage comes with C2.
- **Boot self-test:** from `ss_testvec.h`, as the keyword app does.

**Setup:** a phone 1 m away at normal TV volume. Test clips are ESC-50 fold 5, never trained on.
Teach three sounds:
- a doorbell chime played from the phone;
- a microwave-style beep pattern (generated);
- keys jingling, live.

| # | Measurement |
|---|---|
| 1 | Built-ins: 10 clips per built-in class, giving % recognised and the confusions |
| 2 | Taught: teach the 3 sounds (5 examples each), then 10 trials each: % accepted as the right sound |
| 3 | Rejection: 3 untaught sounds (clapping, a different chime, a cough) × 10: % not matched to any alerting profile |
| 4 | False alarms: 10 min of people talking, music or a video from a laptop, and typing. Count the alerts. Speech is an indicator, not an alert |
| 5 | Latency: onset sample time → decision → `present()` done. Median and p90 |
| 6 | `dropped` (must stay 0), NPU and runner ms, feature ms |
| 7 | **If built-ins fall short:** re-measure their profiles on the board (play 10 fold 1–4 clips per class through the phone, average the board's embeddings, replace the profile) and rerun row 1. This tests whether the profile design absorbs the mic and room difference without retraining |

| Result | Criteria | Then |
|---|---|---|
| **GO** | Built-ins ≥ 80% (after re-measuring if needed), taught ≥ 90%, untaught ≥ 80% rejected, ≤ 1 false alert in 10 min, median latency ≤ 500 ms | Build P0, then P1 |
| **SOFT** | Taught sounds pass but some built-ins don't | Ship only the built-ins that pass (knock, alarm and baby are the most important) and lean on teaching |
| **NO-GO** | Taught sounds fail on the board but passed on the host | It's a level, clipping or windowing problem: compare recorder captures (C4) with the training data before anything else. Report before continuing |

---

## C1. Encoder on the NPU (board, 30 min)

- **Host side:** `export_ss.py` prints the lowered graph's top-level ops (only
  `executorch_call_delegate` allowed), the Vela estimate and the .pte size.
- **Board self-test:**
  - C features vs Python, expected 0 LSB as with the keyword model.
  - Embedding vs host int8, ≤ 1 LSB.
- **Timing:** NPU and runner time, in debug and release.
- **Size:** `.bin` size against the 2 MB partition. `app.bin` is 458 KB in debug today.
- **Pass:** 0 CPU ops, parity ≤ 1 LSB, NPU ≤ 2 ms per inference.

## C2. Saved sounds survive power loss (board, 45 min)

**Module `firmware/soundsense/ss_store.c`.**
- **Driver:** the pack 2.0.4 CMSIS OSPI flash driver, as `flash_test` uses it: components
  `Device:SOC Peripherals:OSPI` and `BSP:External peripherals:OSPI Flash ISSI`, register mode,
  no XIP. Pulse the flash reset (P15_7) before init ([bringup-notes.md](bringup-notes.md#external-memory-ospi)).
- **Layout:** copy A at flash offset `0x00100000`, copy B at `0x00110000`, up to 64 KB each.
  These are far from `flash_test`'s sectors at `0x01F00000` and `0x03F00000`.
- **Record format:** magic `SSND`, version, sequence number, length, CRC32, settings, then per
  profile:
  - preset name and icon id, urgency, LED colour, threshold;
  - up to 10 examples and 10 counter-examples, as int8 64-d embeddings.
- **Saving:** erase and program the *older* copy, then read it back to verify.
- **Booting:** load the valid copy with the highest sequence number.

| # | Measurement |
|---|---|
| 1 | 100 saves while the listening loop runs: `dropped` stays 0? Save-time distribution |
| 2 | Pull USB power in the middle of a save loop, 5 times: a valid copy loads every time? |
| 3 | 5 normal power cycles: profiles intact, and a taught sound is still recognised? |
| 4 | Boot restore time |

**Pass:** 0 dropped audio blocks, save ≤ 300 ms, 5/5 and 5/5. If the flash driver blocks long
enough to drop audio, save only during silence, or split erase and program across idle periods.

## C3. Alerts, LEDs and screen (board, 45 min)

| # | Measurement | Pass |
|---|---|---|
| 1 | Both RGB LEDs, every colour, **confirmed by eye** (never done; pins in [bringup-notes.md](bringup-notes.md#peripherals)). Blink patterns at 2–8 Hz; visible across a room? | Confirmed |
| 2 | Alert card: time from decision to `present()` done, debug and release | ≤ 100 ms |
| 3 | Full-screen strobe: alternate white and red frames at 4 Hz for 10 s while listening. Measure `present()` (it copies 1.15 MB per frame). Also try handing the display a pointer instead (`Driver_CDC200.Control(CDC200_FRAMEBUF_UPDATE_VSYNC, addr)`, in pack 2.0.4's `Driver_CDC200.h`) | `dropped` stays 0 |
| 4 | Live spectrogram strip (200x40 px, scrolling one column per 20 ms) plus top-3 bars: achieved fps | ≥ 10 fps, `dropped` 0 |
| 5 | Backlight: `LCD_BACKLIGHT` is GPIO6_1 (pack `board_defs.h`). Test on/off, then software PWM (about 1 kHz from a timer) for dimming: any flicker? | Dimming works, or document that it's on/off only, in which case night mode uses a dark UI |

## C4. Recorder and mic geometry (board, 40 min)

**`firmware/recorder/`.**
- Capture both PDM channels at 16 kHz, interleaved int16, into PSRAM, using `psram_test`'s
  vendored driver and init. 64 MB holds about 16 minutes.
- Joystick centre starts and stops; the screen shows elapsed time and both channel levels.
- **Host side:** `tools/pull_audio.sh` (`JLinkExe` `savebin`, as `tools/rtt_dump.sh` does), then
  `tools/raw2wav.py`, giving a stereo wav. Measure the dump speed.
- If `savebin` can't read the PSRAM window, record into SRAM instead (about 60 s).

**Mic geometry.**
- Measure the spacing with a ruler and add a photo to `docs/gates/`.
- Record claps from 1 m at left (90°), front (0°) and right (−90°), 5 each.
- Report the GCC-PHAT lag in samples for each direction, and the level difference between the two
  channels on a front clap (their gains differ: 0x800 vs 0xBB0).

**Decides H12 (direction):** a maximum lag of ≥ 3 samples (spacing ≥ about 6.5 cm) means
left/front/right is feasible. Otherwise drop H12.

**Also capture, for G3:** 2 minutes of room background, and the 3 teaching sounds.

## C5. Duty cycle (board, 30 min)

- **Sleep:** replace the busy-polling main loop with `__WFI()` when no new audio block has arrived (E2).
- **Gate the NPU:** run inference only when the level is above the noise floor + 6 dB, and keep
  running for 300 ms after it drops (E1).
- **Measure over 60 s, in a quiet room and with continuous speech:**
  - CPU active % (cycles outside WFI over the total);
  - NPU busy % (summed execute cycles over the total);
  - inferences per second.
- **Optional (E3):** the CMSIS-DSP FFT. Report feature time per frame and the self-test difference
  (≤ 1 LSB is acceptable).
- **Target, not a gate:** CPU active ≤ 10% in a quiet room, NPU busy ≤ 1%, and no loss in G3
  detection with gating on.

---

## What the results unlock

| Result | Scope |
|---|---|
| G1 GO + G3 GO | Build all of P0, then P1 in the order in SOUND-SENSE.md §3 |
| G1 SOFT | 8 examples per taught sound; T4 (fix mistakes) becomes P0; demo uses distinctive sounds |
| G1 NO-GO | Better embeddings first (wider model, more data, PANNs distillation). If they don't help, report: rethink the project |
| G2 GO | F1 felt vs heard is the fusion headline; F2 and F4 become possible |
| G2 NO-GO | Drop F1, F2, F4 and F5; multimodal score about 10/15; F3 remains |
| G3 SOFT | Ship only the built-ins that pass; teaching carries the demo |
| C3 backlight PWM works | Night mode and the brightness knob (A6, X4) dim the backlight; otherwise a dark UI |
| C4 lag ≥ 3 samples | H12 direction becomes P2 |
| C5 numbers | Go on the Stats page (S4) and into the README's performance section |

---

## Report: `docs/GATES-results.md`

Write the G1–G3 rows at the checkpoint (step 8), push, and tell the user. Add C1–C5 as they finish.

```markdown
# Sound Sense gate results (2026-10-18 HH:MM PDT, commit <sha>)

| Test | Result | Key numbers | Notes |
|---|---|---|---|
| G1 encoder | GO / SOFT / NO-GO | 5-way 5-shot int8 x% (float y%); taught accepted a%, untaught rejected b%, AUROC; built-ins c% / false accept d%; params; MACs | |
| G2 IMU | | sample rate; felt % at 20/50/100 cm; heard-only felt %; alignment median ± spread | |
| G3 live | | built-ins %; taught %; untaught rejected %; false alerts / 10 min; latency median / p90 | |
| C1 NPU | | CPU ops; parity LSB; NPU ms debug / release; .bin KB | |
| C2 flash | | save ms; dropped; power-pull 5/5?; power-cycle 5/5? | |
| C3 UI | | LEDs ok?; alert ms; strobe ok?; spectrogram fps; backlight dimming? | |
| C4 mics | | spacing cm; max lag samples; channel level difference dB | |
| C5 duty | | CPU active % quiet / speech; NPU busy %; inferences/s | |

## Decision
(which rows of "What the results unlock" apply, and the biggest surprise)
```
