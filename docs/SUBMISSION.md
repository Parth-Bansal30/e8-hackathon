# Sound Sense: Devpost submission draft

Draft of everything that goes into the Devpost form for
[arm-executorch.devpost.com](https://arm-executorch.devpost.com/). Review it here in git, then paste
section 2 into the form. Every number comes from [GATES-results.md](GATES-results.md) or the
[README](../README.md); if you change a number, change it there first.

Video script and shot list: [VIDEO-SCRIPT.md](VIDEO-SCRIPT.md). Gallery images and captions:
[demo/GALLERY.md](demo/GALLERY.md).

## 0. What the Devpost page asks for

Read in a browser on 2026-10-18 at about 9:20 AM PDT (Overview, Rules, Schedule, Updates,
Resources). The submission form itself needs a Devpost login, so its exact fields and any
hackathon-specific questions still need checking (see section 3).

**Dates (from the page's schedule):** submissions close **Sunday 2026-10-18, 5:00 PM PDT**, and
winners are announced at 5:30 PM PDT. Our own target is still 4:30 PM, with everything ready by
3:30 PM.

**Required for the final submission** (Overview, "Requirements"), and where we cover each one:

| Requirement (page wording, shortened) | Where we cover it | State |
|---|---|---|
| Live demonstration on the Alif E8 Dev Kit | At the judging table; the run of show is [VIDEO-SCRIPT.md](VIDEO-SCRIPT.md) / [DEMO.md](DEMO.md) | Ready to rehearse |
| Public source code repository | github.com/Parth-Bansal30/e8-hackathon is public | **Default branch `main` is out of date (see below)** |
| Fixed release version: a commit hash or release tag | Section 1, "Judged version" | **Owner to decide** |
| README with build, deploy and run instructions | README "Build and run" | Done on `gates` |
| Brief description of the application architecture | README "How it works"; section 2 "How we built it" | Done |
| Where key models and workloads execute | README "Workload placement"; section 2 | Done |
| Basic performance measurements | README "Measured on the board"; section 2 | Done |

Other rules that matter:
- Teams of 2 to 5 people. One person, the Representative, submits for the team.
- Everything in English.
- Own work; open-source use is allowed if its licences are followed and the project builds on it.
  Our credits are in the README ("Credits and licenses").
- The page doesn't mention a demo video. Devpost forms usually have an optional video field
  (YouTube, Vimeo or Youku link). The script is ready if the form asks for one.
- **Judging criteria (from the page):** Functionality & Reliability 20, Performance & Efficiency
  20, Effective Use of the E8 Architecture 15, Multimodal & Sensor Integration 15, Innovation &
  Use-case Value 10, Technical Ambition & Integration 10, Engineering Quality & Demo 10. Each judge
  scores 1 to 5 per category, weighted. This matches the rubric in the brief.

**The `main` problem.** The repo's default branch is `main`, which is 28 commits behind `gates` and
still shows the old spec-only README. A judge who opens the repo link lands on `main`. Pick one
(owner's call):
1. Merge `gates` into `main` before submitting (best: the repo page then shows the real README), or
2. Link Devpost to the judged commit or tag directly (`.../tree/<tag>`), so judges skip `main`.

Doing both is safest.

## 1. Form fields, ready to paste

**Project name:** Sound Sense

**Elevator pitch** (183 characters; Devpost allows 200):

> A home sound monitor for deaf and hard-of-hearing people. It learns your doorbell from 5
> examples, feels a knock on its table, and runs its encoder on the Ethos-U85. Nothing recorded.

**Built with** (Devpost tags): `alif-ensemble-e8`, `arm-cortex-m55`, `arm-ethos-u85`,
`executorch`, `pytorch`, `vela`, `cmsis`, `c`, `c++`, `python`, `numpy`, `bmi323`, `segger-rtt`,
`esc-50`, `speech-commands`

**"Try it out" links:**
- Code: `https://github.com/Parth-Bansal30/e8-hackathon/tree/<judged tag or commit>`
- Build and run: the README at that link, "Build and run".

**Judged version (fixed release):** `<to fill in>`. Use the commit the board was flashed from for
judging. On the board laptop: `git rev-parse HEAD`, after the last firmware change is committed and
pushed. A tag is easier for judges to read, for example `git tag v1.0-judged <hash>` and
`git push origin v1.0-judged`. That publishes the tag, so it's the owner's call.

**Video demo link:** `<YouTube or Vimeo link, unlisted is fine if the form allows it>`.

**Image gallery and thumbnail:** see [demo/GALLERY.md](demo/GALLERY.md). Thumbnail:
`docs/demo/gallery/01_cover.png`.

**Team:** `<names and Devpost usernames, 2 to 5 people>`.

## 2. About the project (paste into "About the project")

Devpost renders Markdown here (headings, bold, lists, links, code blocks). This section avoids
tables because we haven't checked that Devpost renders them; the README has the same facts as
tables.

---

## Inspiration

A doorbell, a knock, the smoke alarm, the baby crying in the next room: deaf and hard-of-hearing
people miss these every day. Flashing doorbell lights cover one sound. Sound recognition on a phone
needs the phone with you, and a microphone alone can't tell a knock on your door from a knock in a
TV show.

We wanted a small box that sits on the table by the door. It should always be on, learn the sounds
of *your* home, flash when something matters, and never record or send anything. The Alif Ensemble
E8 has every part this needs: two microphones, an accelerometer, a screen, LEDs, flash, and an
Ethos-U85 NPU to run the model.

## What it does

Sound Sense runs entirely on the Alif Ensemble E8 DevKit (Cortex-M55 + Ethos-U85).

- **Recognises built-in sounds:** knock, dog barking, baby crying, alarm, glass breaking, siren and
  running water. Speech and background are recognised too, but never alert.
- **Learns your own sounds on the device:** press UP, pick a name (Doorbell, Keys, Microwave and
  others), and make the sound 5 times. Each example is checked ("too quiet", "clipped") and shown
  with its level over the room. A test match follows, then Save. There's no laptop and no
  retraining: teaching builds a profile from the NPU's embeddings, which takes milliseconds.
- **Remembers them:** taught sounds are saved to the board's 64 MB OSPI flash in the background
  without dropping audio, and come back at boot in 2.2 ms.
- **Feels the knock:** the BMI323 accelerometer, read at 800 Hz, tells a knock on the table it sits
  on ("Felt here + heard") from a knock it only hears, like one on the TV ("Heard only (TV?)").
  A heard-only knock shows as a banner, not a full-screen alert.
- **Accessible alerts:** a full-screen card with icon, word and time, in a colour-blind-safe
  palette, plus an LED pattern for each sound. There are three urgency levels, and urgent sounds
  (glass breaking, siren) strobe once a second, which is safe for photosensitivity. Low
  confidence shows as "Maybe: Dog barking?", never as a certain alert.
- **Timeline** of today's sounds, with repeats merged and "felt here" marked.
- **Live view:** a scrolling spectrogram, the room level against its noise floor, and the three best
  matches right now. In a quiet room it reads "Quiet: NPU resting".
- **Stats page,** live from the board: NPU time per run, CPU active %, NPU busy %, save time,
  model size.
- **Boot self-test:** checks both mics, runs the NPU against a stored reference embedding, restores
  saved sounds, and checks the IMU and lights. It's ready 1.5 s after reset, and any failure stays
  on screen.
- **Private by design:** audio is never stored and never leaves the board.

## How we built it

### Architecture

```
 PDM mics (16 kHz) ──> log-mel, 40 bands / 20 ms (M55) ──> 49x40 patch (~1 s) ──int8──> encoder (Ethos-U85)
                                                                                           │ 64-d embedding
 onset detector (M55) ── 0.8 s after a sound starts ──> cosine match against profiles <────┘
                                                           │  built-in (9) + taught (up to 8, flash)
 BMI323 IMU, 800 Hz FIFO (I3C) ── knock felt within ±100 ms? ──┤
                                                           v
                                     alert card + LED pattern + timeline  ──>  480x800 MIPI DSI LCD, RGB LEDs
```

### Where each workload runs

- **Encoder** (DS-CNN, int8, every operator delegated): **Ethos-U85-256 through ExecuTorch**.
  Weights and command stream in MRAM, tensor arenas in SRAM0/1.
- **PDM capture, log-mel features, onset detection:** Cortex-M55 (HP core), code in ITCM, with a
  320 ms audio ring.
- **Cosine matching, teaching, alerts, timeline:** Cortex-M55, data in DTCM.
- **Knock sensing** (BMI323 FIFO over I3C): Cortex-M55.
- **UI drawing** (anti-aliased, SDF fonts, only changed rows redrawn): Cortex-M55, back buffer in
  SRAM1. Display scan-out by the CDC200 + MIPI DSI from a frame buffer in SRAM0.
- **Taught sounds:** OSPI1 flash (64 MB), two copies with CRC32.
- **Power:** the M55 sleeps (WFI) between audio blocks; the live encoder runs only when the room is
  6 dB above its noise floor.
- **Not used:** the M55-HE core and the two Ethos-U55s. An always-on listener on the HE core is
  on the "What's next" list.

### The model and the ExecuTorch path

- **Encoder:** a DS-CNN with 192 channels, 5 depthwise-separable blocks and a 64-d embedding, on a
  49x40 log-mel patch (about 1 s of audio). 219k parameters, 43.2 M MACs.
- **Training (PyTorch):** a cosine classifier over 76 classes (40 ESC-50 categories, 35 Speech
  Commands words, background) for 60 epochs. We keep only the encoder. 10 ESC-50 classes are never
  trained on; they stand in for the sounds users will teach. We also tried 128 channels (74.1%
  5-way 5-shot) and 192 channels with SpecAugment (72.6%); plain 192 channels (75.8%) shipped.
- **Export:** PyTorch -> PT2E int8 quantization (EthosUQuantizer) -> EthosUPartitioner -> Vela for
  `ethos-u85-256` -> `.pte` (240,224 bytes) -> C header in MRAM -> the ExecuTorch runtime on the
  M55 delegates the whole graph to the U85. **All 14 operators run on the NPU, 0 on the CPU.**
- **Matching the training data:** the log-mel pipeline is written in C for the board and in Python
  for training, and kept bit-exact: the board's features match Python's on all 1960 values. The
  board's embedding is within 1 LSB of the host's (cosine 0.9996).

### Matching and teaching

Every sound, built-in or taught, is a profile: the unit-length mean of its examples' embeddings
plus a threshold (the lowest leave-one-out similarity minus 0.05, clamped to 0.5-0.85). A sound at
least 8 dB over the room alerts if its best cosine score clears that profile's threshold; within
0.05 below it, it shows as "Maybe: ...?". A knock alerts if the encoder hears it or the IMU feels
it, and is certain only when both agree. Because teaching just builds a new profile, the device
learns a sound with no training step.

## Measured on the board

All on the DK-E8 (M55-HP at 400 MHz, Ethos-U85-256), release build, 2026-10-18. Logs and method:
`docs/GATES-results.md` in the repo.

- **NPU time per embedding:** 1.00 ms (1.03 ms through the ExecuTorch runner). Vela estimated
  0.53 ms at 1 GHz.
- **Features:** 0.30 ms per update on the M55 (40 log-mel bands every 20 ms).
- **Model:** 219k parameters, 43.2 M MACs, `.pte` 240,224 bytes, all 14 operators on the
  Ethos-U85.
- **Board vs Python:** features 0 of 1960 values differ; embedding within 1 LSB (cosine 0.9996).
- **Power:** CPU active **12.6%** in a quiet room (it was 100%); NPU busy 0.00-0.05% when quiet; 0
  audio blocks dropped.
- **Alert delay:** about 0.85 s after a sound starts. The board decides 0.8 s after the onset so the
  whole sound is in the window.
- **Flash save:** 223-266 ms (median 252 ms) over 101 back-to-back saves of a full record; 1,000
  more saves had a longest of 304 ms. Across all 1,101 saves: 0 audio blocks dropped and 0 failed
  read-backs. Restore at boot: 2.2 ms.
- **Knock felt:** 21 of 22 knocks at 20 cm. IMU FIFO at 805 samples a second, 0 missed.
- **Few-shot (offline, 10 classes never trained on):** 5-way 5-shot 75.8% int8 (75.9% float);
  10-way 63.0%.
- **Built-in sounds:** 88% offline with 4% false accepts. Live, from a laptop speaker on clips never
  used in training: 54% (alarm 75%).
- **Boot to Ready:** 1.5 s, self-test included.
- **Memory:** ITCM 97.6% (all hot code); MRAM 1.03 MB of 2 MB (model, fonts, cold code).

## Challenges we ran into

- **Finding the CPU time.** Sleeping with WFI between audio blocks only took the CPU from 100% to
  90% awake. We sampled the program counter over J-Link (`tools/pc_profile.py`) and found 67% of
  the time in the joystick read: each read of the low-power GPIO costs about 130 µs, and the loop
  did five per pass. Reading the port once every 10 ms took it to 21%. Scrolling the spectrogram
  instead of redrawing it, and skipping unchanged rows, took it to 14%. Reading the IMU every 10 ms
  instead of every 5 ms took it to 12.6%.
- **Saving without dropping audio.** A 4 KB flash erase blocks for up to 124 ms. The save does one
  erase per loop pass, so the 320 ms audio ring never overflows. It keeps two copies with CRC32
  and always overwrites the older one, so if power dies mid-save, boot uses the previous save.
  That's the design; we didn't test pulling the power mid-save.
- **ITCM full.** All the fast code lives in 256 KB of ITCM, now 97.6% full. Boot-only and rare
  code, including the flash driver, moved to MRAM with a section attribute and linker patterns.
- **Felt vs heard.** The IMU had to run at 800 Hz through its FIFO to catch a knock. On our table,
  knocks are felt within about 20 cm (21 of 22) but not at 50 cm (0 of 20), so setup says "place
  me on the table by the door".
- **A speaker isn't a dog.** Built-in sounds played from a laptop speaker in an unfamiliar room were
  recognised 54% of the time, against 88% offline. That's why teaching your own sounds, with your
  own sources in your own room, became the centre of the product.

## Accomplishments that we're proud of

- The whole product runs on the board end to end: listen, recognise, teach, alert, save to flash,
  self-test at boot. 15 of our 16 must-have features are done.
- A custom PyTorch encoder with every operator on the Ethos-U85, at 1.00 ms per run, matching the
  host model within 1 LSB.
- CPU active time cut from 100% to 12.6%, one measured step at a time.
- 1,101 flash saves while listening, with 0 audio blocks dropped.
- Sensor fusion that tells the user whether a knock was on *this* table, not just that it was a
  knock.

## What we learned

- Measure before optimising: the CPU time was in a GPIO read, not in the DSP or the NPU.
- Make the training features and the device features bit-exact first. Then int8 costs almost
  nothing (75.8% vs 75.9% float, offline), and the board matches the host within 1 LSB.
- On-device learning changes the product: built-ins get you started, but the sounds that matter
  are different in every home.
- Fusion helps most when each sensor answers a different question: the microphones say what the
  sound was, the accelerometer says whether it touched this table.

## What's next

- 8 examples per taught sound instead of 5, plus the missing quality checks ("unlike the others",
  "background too loud").
- Fix mistakes from the timeline: a wrong alert becomes a counter-example.
- Urgent alerts that repeat until acknowledged, and a night mode.
- Smoke vs CO alarm from the beep pattern.
- An always-on listener on the M55-HE core that wakes the main core.
- Left/right direction from the two mics (the 8 cm spacing measured well enough to tell them
  apart).
- A relay output for a bed shaker, the standard night alert for deaf people.

## Known limits

- Built-in sounds from a laptop speaker: 54% live vs 88% offline. Teaching your own sounds is the
  strong path.
- Taught sounds at 5 examples are borderline offline: 38% accepted, 79% of untaught sounds rejected.
  They work live at the board; 8 examples is the planned fix.
- Knocks are felt within about 20 cm of the board.
- Alerts come about 0.85 s after a sound starts, against our 0.5 s target.

## Built with

Alif Ensemble E8 DevKit (Cortex-M55 + Ethos-U85-256), ExecuTorch with the Arm Ethos-U backend,
PyTorch (PT2E quantization), Vela, CMSIS-Toolbox, Arm GNU toolchain, Alif SETOOLS, SEGGER J-Link
and RTT, Python (NumPy, SciPy, Pillow). Training data: ESC-50 (CC BY-NC 3.0) and Speech Commands v2
(CC BY 4.0). The firmware builds on alifsemi/alif_vscode-template, and the ExecuTorch runner
started from Arm's alif-ethos-u85-npu-mnist learning path.

---

## 3. Before you submit (owner)

- [ ] Sign in to Devpost and open the submission form. Check for fields this draft doesn't cover
      (hackathon questions such as repo URL or commit hash, team, student status for the all-student
      prize).
- [ ] Decide `main` (merge `gates`, link the tag, or both).
- [ ] Fill in the judged commit or tag (section 1).
- [ ] Record and upload the video if the form asks for one; paste the link.
- [ ] Upload the gallery images (thumbnail first).
- [ ] Add the team members.
- [ ] Check the preview: the code block and the bold text render.
- [ ] Re-check against the board just before pasting. Uncommitted work in progress at 9:20 AM:
      urgent alerts that wait for a press and escalate after 30 s (A5). If it lands, move it
      from "What's next" to "What it does", and from README "Known limits" to "What it does".
      If teaching moves to 8 examples, change "5 times"/"5 examples" in "What it does" and the
      elevator pitch.
- [ ] Submit before 4:30 PM (the page's hard deadline is 5:00 PM PDT).
