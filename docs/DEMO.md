# Sound Sense demo run sheet

Sound Sense is a home sound monitor for deaf and hard-of-hearing people. It runs entirely on the
Alif DK-E8 (Cortex-M55 and Ethos-U85), and nothing is recorded or sent anywhere. Screens for
slides are in [demo/screens.png](demo/screens.png). That sheet comes from the UI simulator, so its
numbers are samples; quote the measured ones below.

## Before you start (5 minutes)

- **Firmware:** the board runs the release build. If in doubt, run
  `tools/build_flash.sh soundsense release` (about 45 s). After a reset the **Ready** screen
  checks the mics, NPU, saved sounds, vibration sensor and lights (1.5 s), then the home screen
  says **Listening**. A red row means something failed; it stays on screen until you press.
- **Placement:** put the board flat on the table where you'll knock. Put the laptop on the same table, 0.5 to 1 m away.
- **Mac sound:** unmute the Mac and set the volume to about 75%. (It was muted at the last check.)
- **Sound check:** in a terminal at the repo root, run `tools/demo_sound.py alarm`. A full-screen **Alarm** should appear.
- **Room:** sounds must be about 8 dB over the room's background, and teaching needs 10 dB. A quiet corner helps.
- **Joystick** (directions as you face the screen):
  - **UP:** teach a sound (or teach the "What was that?" sound while its banner shows).
  - **DOWN:** settings menu: night mode, microphones off, sensitivity (the knob), privacy, Sounds
    (built-ins on/off; taught sounds' urgency, RIGHT renames), Calibrate room (10 s), forget sounds.
  - **LEFT:** timeline.
  - **RIGHT:** stats.
  - **Press:** OK or dismiss.
  - **On an alert or a timeline row:** LEFT marks it wrong, RIGHT marks it right. DOWN on an alert
    snoozes that sound for 10 min.
  - **While teaching:** LEFT starts over, UP makes a flagged example again, press saves, RIGHT cancels.

  Press once and wait for the screen to change; the switches bounce. The app ignores sounds for
  0.8 s after a press, so the click doesn't count.

## Run of show (about 4 minutes)

1. **Pitch (20 s):** say who it's for, and that the AI runs on the NPU on the board, offline, with nothing recorded.
2. **Live view (20 s):** walk through the home screen:
   - the spectrogram, scrolling 10 times a second;
   - the room level against its noise floor;
   - the three best matches right now. In a quiet room this reads **Quiet: NPU resting**: the
     encoder only runs when the room is louder than its floor, to save power. Talk or clap and the
     matches come back.
3. **Built-in alert (30 s):**
   - Run `tools/demo_sound.py alarm`. A full-screen **Alarm** appears and the LEDs blink. Press to dismiss.
   - Optional: `glass` (an urgent orange screen that strobes once a second, safe for photosensitivity) or `baby`.
4. **Knock: felt vs heard (45 s):**
   - Knock on the table about 20 cm from the board. The alert reads **Knock at the door · Felt here + heard**.
   - Now hold the laptop in your hands and run `tools/demo_sound.py knock`. It shows as **Heard only (TV?)**.

   If the laptop sits on the same table, the board feels its speaker, so lift it for this part.
5. **Teach a new sound (90 s):**
   - Press **UP**. The name screen starts on **Keys** (UP/DOWN changes the name). Press to start.
   - Jingle keys 8 times, about 2 s apart. Each circle turns green and shows its dB over the room.
     If one is flagged orange ("unlike"), press UP and jingle once more to replace it.
   - Jingle once more: it shows **Test match: xx%**. Press to **Save**.
   - Jingle the keys again: **Keys** alert.
   - Optional: press reset. The Ready screen shows **Saved sounds: 1 taught sound restored**, and keys still
     alert. Taught sounds live in the flash.

   **Repeatable version with the laptop:** on the name screen, press DOWN once to pick **Doorbell**, then press to start. Then:
   - run `tools/demo_sound.py doorbell --times 8` and wait for 8 green circles;
   - run `tools/demo_sound.py doorbell` to see the test match;
   - press to save;
   - run `tools/demo_sound.py doorbell` again for a **Doorbell** alert.
6. **Timeline (15 s):** press **LEFT** for today's sounds, with repeats merged and "felt here" marked. Press to go back.
7. **Stats (15 s):** press **RIGHT** for NPU time, model size and CPU load, all live from the board.

## Numbers you can quote (measured on this board, 2026-10-18)

- **Encoder:**
  - 219k parameters, 43 M MACs, a 240 KB model, all 14 operators on the Ethos-U85.
  - 1.0 ms per run (1.03 ms through the ExecuTorch runner), 10 runs a second.
- **Accuracy against the Python reference:** the log-mel features match exactly (0 of 1960 values differ), and the embedding is within 1 LSB.
- **Power:** in a quiet room the CPU is awake 12.6% of the time (it sleeps between audio blocks)
  and the NPU is idle; it was 100% before the power work. 0 audio blocks dropped.
- **Saving:** a taught sound is saved to flash in about 250 ms, in the background, without
  dropping audio (1,101 test saves). Restored at boot in 2.2 ms.
- **Boot:** ready 1.5 s after reset, self-test included.
- **Knock sensing:** accelerometer read at 800 Hz through its FIFO. Knocks 20 cm away were felt 21 of 22 times.
- **Alert timing:** a clear match to a built-in sound is on screen about 0.44 s after the sound starts (median 437 ms on the board, 0 wrong in the live run). Anything else waits 0.8 s so the whole sound is in the window: about 0.87 s.
- **Built-ins from a laptop speaker,** on clips never used in training: 54% overall, alarm 75%. Taught sounds work live.

## Known limits (be upfront if asked)

- **Built-ins vs taught sounds:** the built-ins are weaker through a laptop speaker and an unfamiliar room. Teaching is the strong part: 8 examples, on the device.
- **Taught sounds offline:** accepted 45% of the time at 8 examples (38% at 5) on recordings it had
  never heard; live with the same keys or doorbell it works.
- **Knock range:** knocks are felt within about 20 cm of the board on this table, hence "place me on the table by the door".

## If something goes wrong

- **No alert:**
  - Check that the Mac is unmuted and loud enough.
  - Move the laptop closer.
  - The room level bar on the home screen should jump when the sound plays.
- **Wrong label:** the low-confidence path shows "Maybe: …?". That's the cue to teach it.
- **Frozen or blank screen:** press reset or power-cycle; it is ready in about 1.5 s, and taught
  sounds come back from the flash.
- **Teach without the joystick (over J-Link):**

  ```
  E=firmware/out/soundsense/E8-HP/release/soundsense.elf
  tools/jlink.py $E cmd 1 1    # teach Doorbell (0 Keys, 1 Doorbell, 2 Clap, 3 Microwave)
  tools/jlink.py $E cmd 2      # save
  tools/jlink.py $E cmd 3      # back to home / dismiss
  tools/jlink.py $E cmd 17 0   # a teach shot without sound (the boot test clip)
  ```
- **Logs:** `tools/jlink.py firmware/out/soundsense/E8-HP/release/soundsense.elf rtt` prints one
  `[SS]` line per sound: the best match, its score and threshold, and whether it was felt.
