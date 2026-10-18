# Brief: the Sound Sense submission (Devpost write-up, demo video, gallery)

For a separate Claude session that prepares the hackathon submission while the board laptop keeps
working on firmware. Written 2026-10-18, about 9:15 AM PDT.

## The goal

Get a complete, accurate submission for **Sound Sense** onto
[arm-executorch.devpost.com](https://arm-executorch.devpost.com/) before **Sunday 2026-10-18,
4:30 PM PDT**. Aim to have everything ready by 3:30 PM so there is an hour of slack. A submission
that's missing a required part scores nothing for it, so cover every required field first, then
polish.

## Start here

1. `cd` into the repo (`github.com/Parth-Bansal30/e8-hackathon`), `git fetch`, check out branch
   **`gates`** and pull. Check the newest commit across all branches (`git log --all --oneline -5`),
   not just `main`. Work on `gates` or a branch off it; **never push to `main`** without the
   project owner's OK.
2. Read, in this order:
   - [README.md](../README.md): the project as it stands (rewritten today; the best single summary).
   - [STATUS.md](STATUS.md): every feature, done or not, and the gates.
   - [GATES-results.md](GATES-results.md): the measurements behind every number. Quote only these.
   - [DEMO.md](DEMO.md): the demo run sheet; the video follows it.
   - [SOUND-SENSE.md](SOUND-SENSE.md): who it's for, design rules, the full feature list, and §8,
     how it maps to the rubric.
3. Open the Devpost page in a browser (plain fetching gets HTTP 403) and read the **Rules**, the
   **submission form** and the **judging criteria**. Write down every required field, the video
   rules (length, where to host it, public or unlisted) and whether the repo must be public or
   carry a licence. Check them against this brief: the page wins if they disagree.

## Judging rubric (as we understood it; confirm on the page)

| Criterion | Points | What to show |
|---|---|---|
| Functionality & reliability | 20 | Live alerts, teaching on the device, flash save that survives a reset, the boot self-test |
| Performance & efficiency | 20 | 1.0 ms NPU per embedding, CPU active 100% -> 12.6%, NPU gated off in quiet, 0 audio dropped |
| Effective use of hardware | 15 | Ethos-U85 (all 14 ops), M55 DSP/matching/UI, both PDM mics, BMI323 IMU, OSPI flash, LCD, RGB LEDs |
| Multimodal & sensor fusion | 15 | Knock felt (IMU) vs heard (mics): the TV case |
| Innovation & use-case value | 10 | Accessibility for deaf/HoH users, privacy (nothing recorded), learning on the device |
| Technical ambition | 10 | Custom embedding model trained for few-shot, on-device teaching, fusion, persistence, power work |
| Engineering & demo | 10 | Clear video, architecture diagram, honest numbers, a reproducible repo |

## What to produce

1. **Devpost write-up.** Use Devpost's usual sections unless the form says otherwise: Inspiration,
   What it does, How we built it, Challenges, Accomplishments, What we learned, What's next, Built
   with. Pull the facts from the README. Include:
   - the architecture diagram and workload-placement table (README "How it works");
   - the measured-numbers table (README "Measured on the board");
   - the ExecuTorch path: PyTorch -> PT2E int8 -> EthosUPartitioner -> Vela -> .pte in MRAM ->
     ExecuTorch runtime on the M55 delegating to the U85;
   - challenges with real stories (see "Good stories" below);
   - known limits, stated plainly (README "Known limits").
   Save the draft in the repo as `docs/SUBMISSION.md` so the team can review it in git before
   pasting it into Devpost.
2. **Demo video.** Follow [DEMO.md](DEMO.md) "Run of show" (about 4 minutes; cut to the page's
   limit). The project owner records it at the board; your job is the script, the shot list and the
   voice-over text, timed per segment. Shots: Ready screen at boot, live view ("Quiet: NPU resting",
   then matches appear), a built-in alert, knock felt vs heard, teaching a sound start to finish,
   reset and the taught sound still works, timeline, stats. Put the script in
   `docs/VIDEO-SCRIPT.md`.
3. **Gallery images.** Screens rendered by the UI simulator are in `docs/demo/screens/` (one PNG
   per screen, 480x800) and `docs/demo/screens.png` (contact sheet). They are simulator renders,
   so their numbers are samples: say so in captions or use photos of the real board for numbers.
   A photo of the board on the table and an architecture image help.
4. **Repo check.** README renders well on GitHub, links work, nothing secret is committed, licences
   and credits are in place (README "Credits"). If the rules require the code on the default
   branch or a public repo, tell the project owner: merging `gates` into `main` is their call.

## Numbers you can quote (all measured on the board, 2026-10-18)

- Encoder: 219k parameters, 43.2 M MACs, .pte 240,224 B, all 14 operators on the Ethos-U85.
- 1.00 ms per embedding on the NPU (1.03 ms through the ExecuTorch runner); features 0.30 ms on the M55.
- Board matches Python: features 0 of 1960 values differ; embedding within 1 LSB (cosine 0.9996).
- Few-shot (offline, 10 classes never trained on): 5-way 5-shot 75.8% int8; 10-way 63.0%.
- Built-ins: 88% offline with 4% false accepts; 54% live from a laptop speaker (alarm 75%).
- Knock felt: 21 of 22 at 20 cm; IMU FIFO 805 samples/s, 0 missed.
- Flash save: 223–266 ms, 1,101 saves with 0 audio dropped and 0 failed read-backs; restore 2.2 ms.
- Power: CPU active 12.6% in a quiet room (from 100%); NPU busy 0.00% when quiet.
- Ready 1.5 s after reset. Alert about 0.85 s after a sound starts.

Don't round these up or invent new ones. If a number you want isn't in GATES-results.md, leave it
out or ask the board session to measure it.

## Good stories for "Challenges" and "What we learned"

- **Finding the CPU time:** WFI sleep only got the CPU from 100% to 90% awake. Sampling the
  program counter over J-Link (`tools/pc_profile.py`) showed 67% of the time in the joystick read:
  each low-power GPIO read costs about 130 µs. Reading the port once every 10 ms, scrolling the
  spectrogram instead of redrawing it, and polling the IMU less got it to 12.6%.
- **Saving without dropping audio:** a 4 KB flash erase blocks for up to 124 ms. The save does one
  erase per loop pass so the 320 ms audio ring never overflows. It keeps two copies with CRC32 and
  always overwrites the older one, so if power dies mid-save, boot uses the previous save. (That
  is the design; pulling the power mid-save wasn't tested.)
- **ITCM full:** all fast code lives in 256 KB of ITCM, at 97.6%. Boot-only and rare code moved to
  MRAM with a section attribute and linker patterns.
- **Bit-exact features:** the same C feature code runs on the board and in Python, so the encoder
  sees exactly what it was trained on (0 of 1960 values differ).
- **Felt vs heard:** the IMU had to run at 800 Hz through its FIFO; knocks are felt within about
  20 cm, which became the "place me on the table by the door" setup step.
- **Honest limits:** the laptop speaker and room cut built-in accuracy from 88% to 54%, which is why
  teaching your own sounds is the centre of the product.

## Constraints

- **No loud sounds:** the project owner works in a shared room. Don't play test sounds or raise the
  Mac volume. Writing and editing are silent; leave anything that needs sound to the owner.
- The board is attached to the other laptop. Don't flash, reset or talk to it from this session.
- Commit your drafts to `gates` (or a branch off it) and push; pull first, since the board session
  pushes too. Touch only `docs/SUBMISSION.md`, `docs/VIDEO-SCRIPT.md` and new files under
  `docs/demo/`, plus README fixes, so the two sessions don't collide.
- Ask the project owner before submitting on Devpost, before making anything public, and before
  merging to `main`.

## When you're done

Tell the project owner: what's ready to paste into Devpost, what still needs them (recording the
video, team details, the final submit), and any required field you couldn't fill.
