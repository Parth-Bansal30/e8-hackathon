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

**Matches the judged firmware** (recorded on `v1.0-judged` = `32177ea`; the judged `v1.1-judged` =
`1d5fe72` only adds a "Whistle" teach name): 8 examples per
taught sound, the teach checks ("loud room", "unlike the others", "Sounds like …"), the two-stage
alert decision, and the settings and snooze added since 10 AM.

## Before recording

- **Board:** release build, flat on the table where you'll knock. **Don't clear the taught
  sounds:** the board holds the tight Doorbell (threshold 0.85) that the live demo relies on.
  Teaching a name again replaces its old profile, so on camera teach **Keys**, never Doorbell.
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
- **Joystick on an alert:** PRESS is OK (dismiss), LEFT marks it wrong, RIGHT marks it right. Use
  PRESS to dismiss in segments 4 to 7, so you don't mark anything by accident.

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

### 2. Ready self-test (0:20-0:33)

- **Shot:** close on the screen. Press reset. The **Ready** screen ticks through its rows, then
  goes to the home screen.
- **Done when:** all five rows green and "Ready" shown.
- **VO:** "Every start runs a self-test: both microphones, the neural processor against a stored
  reference, the saved sounds, the vibration sensor and the lights. It's ready one and a half
  seconds after reset."
- **On screen:** "Ready 1.5 s after reset"

### 3. Live view (0:33-0:50)

- **Shot:** close on the home screen in a quiet moment: it reads **Quiet: NPU resting**. Then talk
  or clap once near the board: the top 3 matches come back.
- **Done when:** both states are on camera.
- **VO:** "The home screen shows what it hears: a spectrogram, the room level against its noise
  floor, and the three best matches. In a quiet room the NPU rests to save power. When I talk,
  the encoder wakes up and the matches come back."
- **On screen:** "CPU awake 12.6% in a quiet room (was 100%)"

### 4. Built-in alert (0:50-1:12)

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
- **On screen:** "Clear matches on screen in a median 437 ms"

### 5. Knock: felt vs heard (1:12-1:52)

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

### 6. Teach a new sound (1:52-2:55)

- **Shot:** close on the screen, with your hand and keys in frame.
  1. Press **UP**. The name screen starts on **Keys**. Press to start.
  2. Jingle the keys 8 times, about 2 s apart. Each circle turns green and shows its dB over the
     room. If one turns orange ("unlike the others"), press **UP** and jingle once more to replace
     it; that's worth keeping in the cut.
  3. Jingle once more: **Test match: xx%**. Press to **Save**.
  4. Jingle again: a **Keys** alert.
- **Done when:** 8 green circles, the test match, and the Keys alert are all on camera.
- **VO:** "Every home sounds different, so you teach it your own. Press up, pick a name, here Keys,
  and press to start. I jingle my keys eight times. Each example is checked: too quiet, clipped, a
  loud room, or unlike the others gets flagged, and a good one turns green with its level over the
  room. One more jingle tests it. Save. No laptop and no retraining: the board builds a profile
  from the NPU's embeddings, and saves it to flash in the background without missing a sound. And
  now it knows my keys."
- **Fallback (repeatable, uses the laptop):** on the name screen press DOWN three times for
  **Microwave** (not Doorbell: that would replace the live demo's profile), then
  `tools/demo_sound.py microwave --times 8`, then `tools/demo_sound.py microwave` for the test,
  Save, and once more for the alert. Change "keys" to "the microwave" in the VO. If it warns
  "Sounds like …", the new sound is close to one it already knows; cancel (RIGHT) and use the take
  you have.
- **On screen:** "Taught on the device · saved in 223-266 ms, 0 audio dropped"

### 7. Survives a reset (2:55-3:10)

- **Shot:** press reset. The Ready screen's **Saved sounds** row says the taught sounds were
  restored (2 with the Doorbell already on the board). Jingle the keys: **Keys** alert.
- **VO:** "Now I reset the board. The self-test restores the taught sounds from flash in under a
  millisecond. Jingle again, and it still knows my keys."
- **On screen:** "Restored from OSPI flash in 0.7 ms (2.2 ms for a full store)"
- **As recorded (1:33-1:45 PM):** 2 taught sounds restored in 0.7 ms, Ready in 1.48 s, Keys alert
  at 0.74 (`gates/video_takes.txt`).

### 8. Timeline and fixing a mistake (3:10-3:32)

- **Shot:** press **LEFT**. Today's timeline, with the alarm, the knocks and the keys from this
  take. The top row is the selected one (UP/DOWN moves). Pick the laptop knock (or the alarm) and
  press **LEFT**: the row says **Marked wrong**. Press to go back.
- **Mark only a built-in row wrong, never Keys or Doorbell.** A wrong mark on a taught sound is
  saved to flash as a counter-example and vetoes about half its real matches (found by the 1 PM
  screen tour). Never press LEFT on an injected alert (host command 13).
- **VO:** "Left shows today's timeline: every sound, with repeats merged and felt knocks marked.
  If it got one wrong, left marks it. That sound becomes a counter-example, so it won't set off
  the same alert again."
- **After recording:** a wrong mark on a built-in sound lasts until a restart, so reset the board
  before the live demo.

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

### Optional cutaways (if a take runs short)

- **Settings (8 s):** on the home screen press **DOWN**: night mode, microphones off, sensitivity,
  privacy, Sounds (built-ins on/off, urgency, rename), Calibrate room, forget sounds. Overlay:
  "Settings on the board: no app, no account".
- **Snooze (5 s):** on any alert press **DOWN**: that sound is snoozed for 10 minutes. Overlay:
  "DOWN snoozes a sound for 10 min".
- **"What was that?" (8 s):** a loud sound it doesn't know (a pan lid, a cupboard door) gets a
  yellow banner; UP would start teaching it from that sound. Needs about 15 dB over the room.

## 3:00 cut

Same shots, shorter. Cut between teach examples instead of showing every pause.

| Segment | Time | Change from the full cut |
|---|---|---|
| 1. Pitch | 0:00-0:15 | VO: drop "Sound Sense is a small box for the table by the door." |
| 2. Ready | 0:15-0:25 | VO: "Every start runs a self-test of the mics, the NPU, the saved sounds, the vibration sensor and the lights. Ready in one and a half seconds." |
| 3. Live view | 0:25-0:37 | VO: "In a quiet room the NPU rests to save power. When I talk, the encoder wakes up." |
| 4. Built-in alert | 0:37-0:52 | No glass. VO: "Seven sounds are built in. Here's an alarm: icon, word and time, colour-blind-safe, and the lights flash a pattern of their own." |
| 5. Knock | 0:52-1:27 | Full VO from the 4:00 cut. This is the fusion point; don't cut it. |
| 6. Teach | 1:27-2:22 | Cut between the 8 examples (show the first and the eighth landing, and a redo if one was flagged). Full VO. |
| 7. Reset | 2:22-2:34 | Full VO. |
| 8. Timeline and a mistake | 2:34-2:44 | Show one row marked wrong. No VO; overlay "Today's timeline. LEFT marks a mistake, which becomes a counter-example". |
| 9. Stats and close | 2:44-3:00 | VO: "Right shows the numbers, live from the board: our own PyTorch model, all fourteen operators on the Ethos-U85, about one millisecond per run, and the CPU awake twelve point six percent of the time in a quiet room. Sound Sense." |

## After recording

- Export at 1080p. Add the captions: [demo/captions_4min.srt](demo/captions_4min.srt) or
  [demo/captions_3min.srt](demo/captions_3min.srt), made from the VO lines above by
  `docs/demo/make_captions.py` and timed to the script's slots. Nudge them to the real footage, or
  rerun the script after changing a VO line.
- Cards and overlays in [demo/video/](demo/video/) (1920x1080): `title_card.png` to open (2-3 s),
  `end_card.png` to close (4-5 s), and one transparent `overlay_NN_*.png` per "On screen" line, to
  lay over that segment as a lower third. Rebuild with `docs/demo/video/make_cards.py`.
- Upload to YouTube (or Vimeo). "Unlisted" lets judges watch with the link; whether it's
  unlisted or public is the owner's call. Paste the link into the Devpost form and into
  SUBMISSION.md section 1.
- Board photos are already in the gallery (image 2, from `docs/demo/board/`). A sharper still from
  the footage can replace them; see [demo/GALLERY.md](demo/GALLERY.md).
