# Handoff: the last three hours, split across two laptops (2026-10-18)

Written 1:15 PM PDT. Firmware is frozen and tagged: **`v1.0-judged` = `32177ea`** (full:
`32177eaeabfa0ecd9c5577c0500474ee2ca85cb9`). `main` holds all of `gates` (PR #3, 1:07 PM). What is
left is checks and media at the board, and the write-up away from it.

- **Board laptop:** the one with the DK-E8 on USB, J-Link, SETOOLS and the et-venv.
- **Desk laptop:** no board. It has the toolchain, `.venv` (numpy, scipy, Pillow) and `cc` for
  `tools/ui_sim`.

Deadlines: Devpost closes **5:00 PM PDT**; team target **4:30 PM**; everything ready by **3:45 PM**.

## Rules for both

- Work on **`gates`**. Before every push: `git pull --rebase --autostash` (the local `.vscode` edits block a plain rebase), then push. Never push to `main`;
  it moves only through a PR the owner OKs. Never move, delete or re-create the `v1.0-judged` tag.
- Commit **only your own paths** (table below) with `git add <path>`, never `git add -A`. Never
  commit `firmware/.vscode/` or `docs/HANDOFF-gates-dream-canvas.md`.
- **No firmware changes.** The board runs the release build of `32177ea`. If a show-stopper bug
  turns up, stop and ask the owner. A reflash means a new tag, and that's their call.
- **No sound in the room without the owner's OK.** Never raise the Mac volume; use the volume as
  found.
- Pushing images over HTTPS can fail with HTTP 400; use `git -c http.postBuffer=157286400 push`.
- Commit messages start with `[board]` or `[desk]` so each side can see the other's progress in
  `git log`.

| Owner | Paths |
|---|---|
| Board laptop | `docs/STATUS.md`, `docs/GATES-results.md`, `docs/gates/`, `docs/HANDOFF-next.md`, `docs/DEMO.md`, `docs/demo/board/` (new: board photos), `tools/` |
| Desk laptop | `docs/SUBMISSION.md`, `docs/VIDEO-SCRIPT.md`, `docs/demo/` (except `docs/demo/board/`), `README.md`, this file |

If you find a mistake in the other side's files, don't fix it. Tell the owner, or note it in a
commit message.

## Board laptop

In this order. Times are targets.

1. **Hand and eye checks, silent (1:15-1:30).** These are the last two "not yet checked" items.
   - Night mode on (DOWN > menu), then wait 15 s on the home screen: the backlight should go off.
   - Ask the owner to turn the blue R79 knob: a notice with a bar should show, and an
     `[SS] knob:` line should appear in RTT.
   - Record pass/fail in STATUS.md and GATES-results.md. Push as `[board] Hand checks: ...`.
2. **Board photos, silent (1:30-1:45).** The owner takes them on a phone. Landscape, 3:2.
   - `docs/demo/board/board_table.jpg`: the board on the table, screen on, LEDs lit if possible.
   - `docs/demo/board/stats_page.jpg`: the real Stats page (joystick RIGHT), sharp enough to read.
   - Resize to 1500x1000 and keep each under 1 MB (`.venv/bin/python` with Pillow, or `sips`).
     Push as `[board] Board photos`.
3. **Video recording, needs sound: ask the owner first (1:45-2:45).** Follow
   [VIDEO-SCRIPT.md](VIDEO-SCRIPT.md). Pull before you start: the desk laptop is changing it to 8
   examples. The owner films; you run `tools/demo_sound.py` and reset state between takes.
   - **Don't clear the taught sounds** (`ss_cmd 4`). The board holds the tight Doorbell (threshold
     0.85) that the live demo relies on. Teach **Keys** on camera as a second sound instead. If
     Doorbell gets cleared anyway, re-teach it at volume 44 as at 12 PM, and check 4 of 5 trials.
   - **Segment 8 (marking a mistake):** mark only a **built-in** row wrong, never a taught one.
     Marking a taught sound wrong saves a counter-example that vetoes about half its real matches
     (the tour hazard). Never press LEFT on an injected alert (host command 13).
   - After the last take, check the store: Doorbell is taught with threshold 0.85, Important, and
     there are no stray wrong marks. Clean up as after the tour if needed.
   - Don't commit footage to git. The owner AirDrops it to whichever laptop edits.
4. **Live demo rehearsal, needs sound (2:45-3:15).** One timed run of [DEMO.md](DEMO.md) "Run of
   show", using the laptop Doorbell version for teaching. Add the injected-alert hazard to DEMO.md
   "If something goes wrong". Push as `[board] Demo rehearsal: ...`.
5. **Pack for judging (by 3:45).** The release build is flashed (Ready screen passes), the store is
   as above, and the cables and laptop go with the board. Update HANDOFF-next.md to the final state
   and push.

No new measurement runs (G2, G3, calibration) unless the owner asks.

## Desk laptop

1. **VIDEO-SCRIPT.md first (1:15-1:35): recording waits on it.**
   - Segments 6 and 7 and the 3:00 cut table: "five times" and "5 circles" become eight.
   - Mention the "unlike the others" flag and UP to redo, as in DEMO.md step 5.
   - Optional: a 10 s cutaway of Settings > Sounds or snooze.
   - Push as `[desk] VIDEO-SCRIPT: 8 examples`, and tell the owner it's ready.
2. **SUBMISSION.md refresh (1:35-2:30).**
   - `<hash>` becomes `32177ea` (section 1, about line 83, and the top of "About the project",
     about line 103).
   - "5 times" / "5 examples" become 8 in the elevator pitch (keep it at most 200 characters),
     "What it does" and "What's next".
   - Accuracy: taught sounds 45% accepted offline at 8 examples (was 38% at 5); live, a tight
     Doorbell teach was recognised in 4 of 5 trials.
   - Alert delay: replace "about 0.85 s, against our 0.5 s target". The two-stage decision gives a
     median of 437 ms for a clear built-in match, otherwise about 0.87 s.
   - Add the features built since 10 AM:
     - Settings > Sounds: built-ins on/off, urgency and renaming.
     - Calibrate room.
     - Snooze (A8).
     - "Still sounding" (H5).
     - "Someone is talking nearby" (H8).
     - The "loud room", "unlike the others" and "Sounds like …" checks while teaching (T2, T6).
   - Known limits: built-ins 54% from a laptop speaker. Once the board laptop pushes the hand
     checks, update the "not yet checked by hand" line.
   - Update the requirement table's "State" column and the section 3 checklist.
   - Every number must match [GATES-results.md](GATES-results.md) and the README. Quote only those.
3. **Gallery (2:30-3:00).**
   - `make -C tools/ui_sim`, then `.venv/bin/python docs/demo/gallery/make_gallery.py`. `04_teach.png`
     still shows five examples; the screens already show 8.
   - Consider 08 with the Sounds page (`26_menu_sounds`) or snooze (`30_snoozed`).
   - In GALLERY.md, change the 04 caption's "five" to eight.
   - When `docs/demo/board/` lands, add both photos. Use `board_table.jpg` as the thumbnail if it
     looks good. Write their captions.
4. **Captions (3:00-3:15).** Write `docs/demo/captions.srt` from the voice-over lines in
   VIDEO-SCRIPT.md, timed to the cut the owner records. The timing will need nudging to the real
   footage. Captions matter for a product for deaf and hard-of-hearing people.
5. **README pass (3:15-3:30).**
   - Check the README against the Devpost requirements: build and run, architecture, workload
     placement and measurements.
   - Check it against the final hand-check results.
   - The tag's README stays as it was at `32177ea`; fixes reach judges through `main`.
6. **PR `gates` → `main` (by 3:45).** Open it with a short summary of the docs changes. The owner
   OKs and merges it.

## Owner only (no Claude)

- Film the video and the photos (board laptop steps 2-3), and record the voice-over.
- Edit the video. Upload it to YouTube or Vimeo (unlisted is fine) and paste the link into
  SUBMISSION.md, or give it to the desk laptop.
- On Devpost (draft: devpost.com/software/1491064):
  - add the team
  - paste section 2 of SUBMISSION.md
  - upload the gallery, thumbnail first
  - check the preview
  - tick the rules box and **Submit before 4:30 PM**

## Sync points

| When | What | From → to |
|---|---|---|
| ~1:30 | Hand-check results pushed | board → desk |
| ~1:35 | VIDEO-SCRIPT at 8 examples pushed | desk → board |
| ~1:45 | Board photos pushed | board → desk |
| ~3:15 | Footage ready for editing | owner |
| ~3:45 | Board packed; PR open | both → owner |
| 4:30 | Submitted | owner |
