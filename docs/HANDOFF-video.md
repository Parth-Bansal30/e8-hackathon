# Handoff: recording the video, then back to judging (board laptop, 2026-10-18 ~4:15 PM)

For the board laptop and its Claude session. The script is [VIDEO-SCRIPT.md](VIDEO-SCRIPT.md):
one take with live narration, about 2:30. The desk laptop wrote this file and owns VIDEO-SCRIPT.md,
SUBMISSION.md and the README. The board laptop owns the board.

**Clock.** Submissions close at **5:00 PM** and judging at the board runs 5:00-5:30. Record by
**4:35**, upload by **4:45**, paste the link into Devpost by **4:50**, and have the board back in its
judging state (part C) by **4:55**. The video is optional on the form, so if time runs out,
judging comes first.

**No firmware changes.** Don't rebuild or reflash: the flashed release build is the judged
`v1.1-judged` = `1d5fe72`. Don't clear taught sounds (no `ss_cmd 4`, no Settings > Forget).

```
E=firmware/out/soundsense/E8-HP/release/soundsense.elf
```

## A. Before filming (about 5 min)

1. **Firmware check:** press reset on the board. The **Ready** screen should show 5 green rows and
   **6 taught sounds restored** (Doorbell, Keys, Microwave, Clap, Phone, Whistle).
2. **Settings at their defaults** (look, don't change unless one is off): microphones on, night
   mode off, nothing snoozed (DOWN > Sounds), all built-ins on, the blue knob near the middle.
3. **Placement:**
   - The board lies flat on a **hard** part of the table. The leather desk pad muffles knocks to
     +3 to +10 dB, which gives "Maybe: Knock?".
   - The screen faces the camera.
   - Leave slack in the USB cable so a knock doesn't tug on it.
   - Leave room right beside the board for the knuckle knock.
4. **Silence notifications:**
   - **Mac:** Focus / Do Not Disturb on, and quit Slack and Messages. The board's **Phone** sound
     was taught from this laptop's chime, so a notification can set off a Phone alert mid-take.
   - **The filming phone:** Do Not Disturb on too, since a ringtone can trigger Phone as well.
5. **Laptop for the "Heard only" shot (segment 4):**
   - Open a terminal with `tools/demo_sound.py knock` typed, ready to run.
   - The knock has to be **lifted off the board's table** (held up or put on a chair). If it
     plays from the same table, the board feels it, and it shows Felt here + heard.
   - The sound needs about 8 dB over the room: watch the level bar on the home screen. The owner
     sets the volume. The 1:40 PM take used volume 100 for +21 dB; never raise it more than the
     room allows.
6. **Props:** keys within reach but out of shot. The person who taught the whistle does the whistle,
   at the same pitch.
7. **Camera:**
   - Phone in landscape, 1080p, about 45° above the board so the screen fills most of the frame.
   - Press and hold on the LCD to lock focus and exposure, or the white alert cards blow out.
   - Auto-brightness off; enough battery and storage.
8. **Log (needed for C2):** run `tools/rtt_log.sh soundsense release docs/gates/video_takes_2.log`
   in a second terminal and leave it running through the take. It records the board's decision
   for each sound and the new Keys threshold (`[TEACH] saved 'Keys' thr ...`), which the teach
   screen doesn't show.
9. **One silent dry run, not filmed (30 s):** one knuckle knock (expect Knock · Felt here + heard,
   then press to dismiss), and check the camera framing. That's all; don't run the teach.

## B. While filming

- Follow [VIDEO-SCRIPT.md](VIDEO-SCRIPT.md), segments 1 to 8. If one fails, try once more, then move
  on (see "If something fails on camera" there).
- **Press** (centre) dismisses alerts. **Never LEFT or RIGHT on an alert:** they save a wrong or
  right mark, and for a taught sound that is kept in flash. RIGHT for the stats only on the home
  screen.
- Don't open the DOWN menu (snooze, night mode, mic off). Whatever is changed there stays on for
  judging.
- **Teach only the name Keys** (the first one on the name screen). That replaces the old Keys
  profile, threshold 0.50 with one wrong mark from the 1:40 PM takes, and clears its counter-examples.
  Teaching any other name, especially Doorbell, would replace a sound the judging demo uses.
- Stay silent during the 8 jingles. Jingle 3 s apart, 30-50 cm from the board: closer clips, and
  quicker ones get flagged "loud room". If a circle turns orange ("unlike"), press UP and jingle
  once more.

## C. After filming: back to the judging state (by 4:55)

1. Stop the RTT logger.
2. **Check the new Keys threshold:** `grep "saved 'Keys'" docs/gates/video_takes_2.log`.
   - **0.60 or higher:** keep Keys.
   - **Below 0.60** (0.50 is the floor): DOWN > **Forget Keys** (two presses) before judging; not the "Forget all 6 taught" row below it. In
     the 1:50 PM rehearsal the loose Keys (0.50) was the best match for several knocks, and one
     reached 0.52; only the accidental wrong mark stopped it, and the new teach clears that mark.
     The board then holds 5 taught sounds.
3. Press **reset**. The Ready screen should still show 5 green rows and **6 taught sounds restored**
   (the new Keys replaced the old one), or 5 if Keys was forgotten in step 2.
4. If Keys was kept, jingle the keys once: a **Keys** alert. Press to dismiss.
5. Knock once with your knuckles: **Knock · Felt here + heard**, not Keys. Press to dismiss.
6. Check that the settings are still at their defaults (A2).
7. Leave Mac Focus on through judging. `tools/demo_sound.py` still plays; only notifications are
   silenced.
8. Laptop volume for judging (owner's call, from [DEMO.md](DEMO.md)): Doorbell at about 44%,
   alarm and chime3 at 75-100%.
9. Take the board, the laptop and the cable to the judging table.

## D. Upload and link (by 4:50)

1. From the phone: YouTube, **Unlisted** (or public, the owner's call). Title: "Sound Sense: home
   sound alerts on the Alif E8 (Arm + ExecuTorch Edge AI Challenge)". Automatic captions are
   enough.
2. On Devpost, open the Sound Sense draft, go to **Project details > Video demo link**, paste the
   URL and save. If the project is already submitted, edits are still allowed until 5:00 PM.
3. Send the link to the desk laptop. It fills in `docs/SUBMISSION.md` section 1 and ticks the
   checklist.
4. Optional, if there is time: commit `docs/gates/video_takes_2.log` with a `[board]` prefix. Commit
   nothing else.
