# Sound Sense: demo video script and shot list

For the project owner, recording at the board. It follows the "Run of show" in [DEMO.md](DEMO.md);
use DEMO.md for setup, sound commands and what to do if something goes wrong. The write-up is in
[SUBMISSION.md](SUBMISSION.md).

**Length.** The Devpost page doesn't set a video length (checked 2026-10-18); check the
submission form when signed in. The full cut below runs **4:00**; the **3:00 cut** (end of this
file) drops the optional shots and shortens the voice-over. If the form sets a limit, use the cut
that fits.

**Every number in the voice-over** comes from [GATES-results.md](GATES-results.md) or the README.
The Stats page shows live values, which can differ a little from the voice-over; that's expected.

**If the board changes before you record:** the teach count is in segments 6 and 7 ("five times",
"5 circles"). If the board moves to 8 examples (HANDOFF-next step 4), change both to eight. If the
alert delay changes, the voice-over doesn't quote it, so nothing else changes.

## Before recording

- **Board:** release build, flat on the table where you'll knock. Taught sounds cleared if you want
  the teach segment to start clean (`tools/jlink.py <elf> set ss_cmd 4`, see DEMO.md).
- **Camera:** a phone on a stand, landscape, 1080p, about 45° above the board so the screen fills
  most of the frame. Lock focus and exposure on the LCD (press and hold on the phone screen), or the
  white alert cards will blow out. Turn off auto-brightness on the phone.
- **Second angle (optional):** a wide shot showing the table, your hand knocking, and the laptop
  held in the air. It makes segment 5 clear without words.
- **Sound in the room:** you control it. The keys teach (segment 6) is quiet. The laptop clips
  (alarm, knock) need about 8 dB over the room, at whatever volume the room allows.
- **Voice-over:** record it separately afterwards in a quiet spot and lay it over, or skip it and
  use the captions. **Captions are recommended either way:** the product is for deaf and
  hard-of-hearing people, and a captioned video shows we mean it. Caption text = the voice-over
  lines below.
- **Dismissing:** the alert footer says "Tap anywhere to dismiss", but touch isn't wired up. Press
  the joystick, and say "press" in the voice-over.

## Full cut (4:00)

Times are cumulative. "VO" is the voice-over (about 2.5 words a second). "On screen" is optional
overlay text for the edit.

### 1. Pitch (0:00-0:20)

- **Shot:** wide shot of the board on the table by a door (or just on the table), then push in to
  the screen showing **Listening**.
- **VO:** "Deaf and hard-of-hearing people miss the sounds that matter at home: the door, the
  alarm, the baby. Sound Sense is a small box for the table by the door. It listens, alerts on its
  screen and lights, and runs entirely on this Alif E8 board. Nothing is recorded."
- **On screen:** "Sound Sense · Alif Ensemble E8 · Cortex-M55 + Ethos-U85"

### 2. Ready self-test (0:20-0:35)

- **Shot:** close on the screen. Press reset. The **Ready** screen ticks through its rows, then
  goes to the home screen.
- **Done when:** all five rows green and "Ready" shown.
- **VO:** "Every start runs a self-test: both microphones, the neural processor against a stored
  reference, the saved sounds, the vibration sensor and the lights. It's ready one and a half
  seconds after reset."
- **On screen:** "Ready 1.5 s after reset"

### 3. Live view (0:35-0:55)

- **Shot:** close on the home screen in a quiet moment: it reads **Quiet: NPU resting**. Then talk
  or clap once near the board: the top 3 matches come back.
- **Done when:** both states are on camera.
- **VO:** "The home screen shows what it hears: a spectrogram, the room level against its noise
  floor, and the three best matches. In a quiet room the NPU rests to save power. When I talk,
  the encoder wakes up and the matches come back."
- **On screen:** "CPU awake 12.6% in a quiet room (was 100%)"

### 4. Built-in alert (0:55-1:20)

- **Shot:** run `tools/demo_sound.py alarm` on the laptop (keep the laptop out of frame or at the
  edge). The full-screen **Alarm** card appears and the LEDs blink. Hold 3 seconds on the LEDs, then
  press to dismiss.
- **Optional (full cut only):** `tools/demo_sound.py glass` for the urgent orange screen that
  strobes once a second. Urgent alerts don't time out; press to dismiss. (Left alone for 30 s, the
  strobe doubles and the screen says "No response for ...": worth a 3-second cutaway if you have
  the time.)
- **VO:** "Seven sounds are built in: knock, dog, baby, alarm, glass, siren and running water. Here's
  an alarm: a full-screen card with an icon, a word and the time, in colours that work with colour
  blindness, and the lights flash a pattern of their own. Urgent sounds, like breaking glass,
  strobe once a second and stay until you press."
- **Fallback:** if the alarm shows as "Maybe: Alarm?", keep it: it's the honest low-confidence
  style. Say "When it isn't sure, it says so."

### 5. Knock: felt vs heard (1:20-2:00)

- **Shot A:** knock on the table about 20 cm from the board. The card reads **Knock**, tagged
  **Felt here + heard**. Dismiss.
- **Shot B:** pick the laptop up and hold it in the air, then run `tools/demo_sound.py knock`. A
  banner reads **Heard only (TV?)**.
- **Done when:** both labels are readable on camera. If the laptop stays on the table, the board
  feels its speaker; lift it.
- **Fallback:** "Maybe: Knock?" with "Felt here" means the table felt it but the encoder wasn't
  sure (it's certain only when both agree). Knock a little harder and take it again.
- **VO:** "Here's what a microphone alone can't do. I knock on the table, twenty centimetres from
  the board. The accelerometer, read eight hundred times a second, feels it, and the microphones
  hear it: felt here and heard. Now the same knock from the laptop, held in the air, like a knock on
  TV. The microphones hear it, but nothing touched the table, so it's only a banner: heard only."
- **On screen:** "Knocks felt at 20 cm: 21 of 22"

### 6. Teach a new sound (2:00-3:05)

- **Shot:** close on the screen, with your hand and keys in frame.
  1. Press **UP**. The name screen starts on **Keys**. Press to start.
  2. Jingle the keys 5 times, about 2 s apart. Each circle turns green and shows its dB over the
     room.
  3. Jingle once more: **Test match: xx%**. Press to **Save**.
  4. Jingle again: a **Keys** alert.
- **Done when:** 5 green circles, the test match, and the Keys alert are all on camera.
- **VO:** "Every home sounds different, so you teach it your own. Press up, pick a name, here Keys,
  and press to start. I jingle my keys five times. Each example is checked: too quiet or clipped
  gets flagged, and a good one turns green with its level over the room. One more jingle tests
  it. Save. No laptop and no retraining: the board builds a profile from the NPU's embeddings, and
  saves it to flash in the background without missing a sound. And now it knows my keys."
- **Fallback (repeatable, uses the laptop):** on the name screen press DOWN once for **Doorbell**,
  then `tools/demo_sound.py doorbell --times 5`, then `tools/demo_sound.py doorbell` for the test,
  Save, and once more for the alert. Change "keys" to "doorbell" in the VO.
- **On screen:** "Taught on the device · saved in 223-266 ms, 0 audio dropped"

### 7. Survives a reset (3:05-3:20)

- **Shot:** press reset. The Ready screen's **Saved sounds** row says the taught sound was restored.
  Jingle the keys: **Keys** alert.
- **VO:** "Now I reset the board. The self-test restores the taught sound from flash, in two
  milliseconds. Jingle again, and it still knows my keys."
- **On screen:** "Restored from OSPI flash in 2.2 ms"

### 8. Timeline (3:20-3:32)

- **Shot:** press **LEFT**. Today's timeline, with the alarm, the knocks and the keys from this
  take. Hold 4 seconds, press to go back.
- **VO:** "Left shows today's timeline: every sound, with repeats merged and felt knocks marked, so
  you can see what you missed."

### 9. Stats and close (3:32-4:00)

- **Shot:** press **RIGHT**. Hold on the Stats page for at least 8 seconds so the live numbers can
  be read. End on a wide shot of the board on the table, or on the home screen.
- **VO:** "Right shows the numbers, live from the board. The encoder is our own PyTorch model,
  quantized to int8 and run through ExecuTorch, with all fourteen operators on the Ethos-U85: about
  one millisecond per run. In a quiet room the CPU is awake twelve point six percent of the time,
  down from a hundred. Sound Sense learns your home, feels your door, and keeps your sounds to
  itself."
- **On screen:** "NPU 1.00 ms per embedding · all 14 ops on the Ethos-U85 ·
  github.com/Parth-Bansal30/e8-hackathon"

## 3:00 cut

Same shots, shorter. Cut between teach examples instead of showing every pause.

| Segment | Time | Change from the full cut |
|---|---|---|
| 1. Pitch | 0:00-0:15 | VO: drop "Sound Sense is a small box for the table by the door." |
| 2. Ready | 0:15-0:25 | VO: "Every start runs a self-test of the mics, the NPU, the saved sounds, the vibration sensor and the lights. Ready in one and a half seconds." |
| 3. Live view | 0:25-0:37 | VO: "In a quiet room the NPU rests to save power. When I talk, the encoder wakes up." |
| 4. Built-in alert | 0:37-0:52 | No glass. VO: "Seven sounds are built in. Here's an alarm: icon, word and time, colour-blind-safe, and the lights flash a pattern of their own." |
| 5. Knock | 0:52-1:27 | Full VO from the 4:00 cut. This is the fusion point; don't cut it. |
| 6. Teach | 1:27-2:22 | Cut between the 5 examples (show the first and the fifth landing). Full VO. |
| 7. Reset | 2:22-2:34 | Full VO. |
| 8. Timeline | 2:34-2:40 | No VO; overlay "Today's timeline: repeats merged, felt knocks marked". |
| 9. Stats and close | 2:40-3:00 | Full VO. |

## After recording

- Export at 1080p. Add the captions (the VO lines above).
- Upload to YouTube (or Vimeo). "Unlisted" lets judges watch with the link; whether it's
  unlisted or public is the owner's call. Paste the link into the Devpost form and into
  SUBMISSION.md section 1.
- Take two stills from the footage for the gallery (see [demo/GALLERY.md](demo/GALLERY.md)): the
  board on the table, and the real Stats page.
