# Sound Sense: demo video script

One take at the board with live narration, about 2:30, no editing needed. Board preparation,
the rules while filming and putting the board back for judging are in
[HANDOFF-video.md](HANDOFF-video.md). Setup and fixes for each sound are in [DEMO.md](DEMO.md).

**Length.** Devpost sets no video length: the rules, the overview and the submission form were
checked on 2026-10-18 at 4:10 PM. The video field is optional and takes a YouTube or Vimeo URL;
it is embedded at the top of the project page. Judging is live at the board (5:00-5:30 PM), so the
video is short and recorded in one take.

**Matches the judged firmware** `v1.2-judged` = `0d8b0f1`. Every number in the narration comes from
the README, [DEMO.md](DEMO.md) or [GATES-results.md](GATES-results.md). The Stats page shows live
values, which can differ a little from what is said.

## While filming

- **Talk before and after each sound, never during one.** While teaching, a voice can become one
  of the 8 examples.
- **Press** (centre) dismisses an alert. **Never LEFT or RIGHT on an alert:** they mark it wrong or
  right, and for a taught sound that is saved to flash. Dismiss first, then use RIGHT on the home
  screen for the stats.
- Don't open the DOWN menu and don't snooze: anything changed there stays on for judging.

## Shot list (2:35)

| # | Time | Do | The screen shows |
|---|---|---|---|
| 1 | 0:00-0:15 | Board on the table, home screen | Listening |
| 2 | 0:15-0:27 | Press **reset** | **Ready**: 5 green rows, "6 taught sounds restored" |
| 3 | 0:27-0:45 | Stay silent for 5 s, then speak (line 3) | **Quiet: NPU resting**, then **Someone is talking nearby** |
| 4 | 0:45-1:15 | Knock hard with your knuckles right beside the board, on the hard table (not the desk pad). Press. Then play `tools/demo_sound.py knock` from the laptop **lifted off the table** | **Knock · Felt here + heard**, then the **Heard only (TV?)** banner |
| 5 | 1:15-1:25 | Whistle the taught note, about 30 cm away. Press | **Whistle** alert |
| 6 | 1:25-2:05 | **UP**: the name screen starts on **Keys**. Press. Jingle 8 times, 3 s apart, 30-50 cm away. Jingle once more for the test, press to **Save**, then jingle again. Press | 8 green circles, **Test match: xx%**, then the **Keys** alert |
| 7 | 2:05-2:25 | **RIGHT** on the home screen; hold for 8 s or more | **Under the hood**: live stats |
| 8 | 2:25-2:35 | Press to go back; end on the home screen | Home |

Segment 6 teaches Keys again, which replaces the old Keys profile and clears its wrong mark from
the 1:40 PM takes; the other five taught sounds are untouched. Don't pick any other name. After the
take, check the new Keys threshold ([HANDOFF-video.md](HANDOFF-video.md), C2): a loose Keys can
catch knocks during judging.

## What to say

1. "Deaf and hard-of-hearing people miss the sounds that matter at home: a knock, an alarm, the
   doorbell. This is Sound Sense. It runs entirely on this Alif E8 board. Our own int8 model runs
   on the Ethos-U85 NPU, offline, and nothing is recorded."
2. "Every boot runs a self-test: both mics, the NPU against a stored reference, the saved sounds,
   the motion sensor and the lights. It's ready in about a second and a half, with everything it
   learned restored from flash."
3. (after the 5 s of silence) "In a quiet room the NPU rests, and the CPU is busy only about 12%
   of the time. When I talk, the encoder wakes up and it knows someone is talking."
4. (after both knocks) "A real knock shakes the table. The board reads its accelerometer 800 times
   a second and fuses that with what the mics hear: felt and heard means someone is at your door.
   The same knock from a speaker is heard but not felt, so it's flagged as maybe the TV."
   **Don't cut this segment:** it is the sensor-fusion point.
5. (before whistling) "We taught it this whistle earlier today."
6. (before the first jingle) "And it learns your home: eight examples, learned on the board in
   seconds." (after the Keys alert) "Each example becomes an embedding from the NPU, averaged into a
   profile and saved to flash. No retraining, no cloud."
7. "These are live numbers from the board: about one millisecond on the NPU per check, all
   fourteen operators delegated through ExecuTorch, ten checks a second, and zero audio dropped.
   The Cortex-M55 does the audio features, the motion sensor, the matching and the screen."
8. "Sound Sense learns your home, feels your door, and keeps your sounds to itself."

## If something fails on camera

Keep filming: press to dismiss, try once more, and if it fails again, go on to the next segment.

- **The knock shows "Maybe: Knock?":** it was felt but too quiet. Knock louder, right beside the
  board. You can say "Maybe is the low-confidence path".
- **No Heard only banner:** the laptop is too quiet, or it is on the table (then it is felt too).
  Turn it up as far as the room allows, or skip that half.
- **A teach circle turns orange ("unlike"):** press **UP** and jingle once more to replace it.
- **No Keys alert after saving:** jingle once more, the same way as the 8 examples.

## After filming

- Upload from the phone: YouTube, **Unlisted** (or public, the owner's call). Title: "Sound Sense:
  home sound alerts on the Alif E8 (Arm + ExecuTorch Edge AI Challenge)".
- Paste the link into the Devpost draft (Project details, **Video demo link**), save, and into
  [SUBMISSION.md](SUBMISSION.md) section 1.
- Captions: YouTube's automatic English captions. `demo/captions_3min.srt` and
  `demo/captions_4min.srt`, and the overlays in `demo/video/`, were made for the earlier 4:00 plan
  (multi-shot, separate voice-over; in git at `fd1d41f`) and don't match this take.
  `demo/video/end_card.png` still fits if there is time to add it.
- Put the board back for judging: [HANDOFF-video.md](HANDOFF-video.md), part C.
