# Sound Sense: Devpost submission draft

Draft of everything that goes into the Devpost form for
[arm-executorch.devpost.com](https://arm-executorch.devpost.com/). Review it here in git, then paste
section 2 into the form. Every number comes from [GATES-results.md](GATES-results.md) or the
[README](../README.md); if you change a number, change it there first.

Video script and shot list: [VIDEO-SCRIPT.md](VIDEO-SCRIPT.md). Gallery images and captions:
[demo/GALLERY.md](demo/GALLERY.md).

## 0. What the Devpost page asks for

Read in a browser on 2026-10-18 at about 9:20 AM PDT (Overview, Rules, Schedule, Updates,
Resources). The submission form was read at about 10:20 AM, after a draft project was created
(devpost.com/software/1491064, submission 1245518; not submitted).

**The form has four steps and no hackathon-specific questions:**
1. **Manage team:** invite teammates by email or a secret invite link. Teams are 2 to 5 people, so
   add everyone.
2. **Project overview:** *Project name*, *Elevator pitch* (200 characters max), thumbnail (JPG, PNG
   or GIF, 5 MB max, 3:2 recommended). Starred fields are required.
3. **Project details:** *About the project* (Markdown, with LaTeX; images by URL), *Built with* (up
   to 25 tags), "Try it out" links (several allowed), image gallery (up to 15 images, JPG, PNG or
   GIF, 5 MB each, 3:2 recommended), video demo link (YouTube or Vimeo, optional).
4. **Submit:** a required checkbox agreeing to the Official Rules and Devpost's Terms of Service for
   the whole team, then "Submit project".

Since there's no field for the judged commit or tag, it goes in two places: a "Try it out" link to
the tag, and a line at the top of "About the project" (both in this draft).

**Dates (from the page's schedule):** submissions close **Sunday 2026-10-18, 5:00 PM PDT**, and
winners are announced at 5:30 PM PDT. Our own target is still 4:30 PM, with everything ready by
3:30 PM.

**Required for the final submission** (Overview, "Requirements"), and where we cover each one:

| Requirement (page wording, shortened) | Where we cover it | State |
|---|---|---|
| Live demonstration on the Alif E8 Dev Kit | At the judging table; the run of show is [VIDEO-SCRIPT.md](VIDEO-SCRIPT.md) / [DEMO.md](DEMO.md) | Rehearsal at the board about 2:45 PM |
| Public source code repository | github.com/Parth-Bansal30/e8-hackathon is public | Done; `main` has all of `gates` (PR #3, 1:07 PM); the docs below reach it through one more PR |
| Fixed release version: a commit hash or release tag | Tag `v1.0-judged` = `32177ea`, the commit the board is flashed from | Done (pushed 12:45 PM) |
| README with build, deploy and run instructions | README "Build and run" | Done on `gates` |
| Brief description of the application architecture | README "How it works"; section 2 "How we built it" | Done |
| Where key models and workloads execute | README "Workload placement"; section 2 | Done |
| Basic performance measurements | README "Measured on the board"; section 2 | Done |

Other rules that matter:
- Teams of 2 to 5 people. One person, the Representative, submits for the team.
- Everything in English.
- Own work; open-source use is allowed if its licences are followed and the project builds on it.
  Our credits are in the README ("Credits and licenses").
- The page doesn't require a demo video; the form has an optional YouTube or Vimeo link, embedded
  at the top of the project page. Worth adding: judges who miss the live demo see it there.
- **Judging criteria (from the page):** Functionality & Reliability 20, Performance & Efficiency
  20, Effective Use of the E8 Architecture 15, Multimodal & Sensor Integration 15, Innovation &
  Use-case Value 10, Technical Ambition & Integration 10, Engineering Quality & Demo 10. Each judge
  scores 1 to 5 per category, weighted. This matches the rubric in the brief.

**`main`.** The repo's default branch is `main`, which judges land on. It was 28 commits behind
`gates` at 9:20 AM; the owner merged `gates` into it at 9:53 AM (PR #1). As agreed at about
10 AM, the board session tagged its last firmware commit `v1.0-judged` (`32177ea`, pushed 12:45 PM),
and `gates` was merged into `main` again (PR #2 at 12:48 PM, PR #3 at 1:07 PM). The tag stays
where it is; later docs changes reach `main` through a PR the owner OKs.

## 1. Form fields, ready to paste

**Project name:** Sound Sense

**Elevator pitch** (183 characters; Devpost allows 200):

> A home sound monitor for deaf and hard-of-hearing people. It learns your doorbell from 8
> examples, feels a knock on its table, and runs its encoder on the Ethos-U85. Nothing recorded.

**Built with** (Devpost tags): `alif-ensemble-e8`, `arm-cortex-m55`, `arm-ethos-u85`,
`executorch`, `pytorch`, `vela`, `cmsis`, `c`, `c++`, `python`, `numpy`, `bmi323`, `segger-rtt`,
`esc-50`, `speech-commands`

**"Try it out" links** (one per field; "add another link" adds a field):
- `https://github.com/Parth-Bansal30/e8-hackathon/tree/v1.0-judged` (the judged version; works
  once the tag is pushed)
- `https://github.com/Parth-Bansal30/e8-hackathon` (the repo; `main` shows the README with build
  and run steps)

**Judged version (fixed release):** tag `v1.0-judged`, commit `32177ea` (full:
`32177eaeabfa0ecd9c5577c0500474ee2ca85cb9`). The board is flashed for judging with the release build
of this commit; the firmware sources haven't changed since `ed20a3e`.

**Video demo link:** `<YouTube or Vimeo link, unlisted is fine if the form allows it>`.

**Image gallery and thumbnail:** see [demo/GALLERY.md](demo/GALLERY.md). Thumbnail:
`docs/demo/gallery/01_cover.png`.

**Team:** HashMap. Add every member in the form's step 1 (invite by email or the secret link); teams are 2 to 5 people.

## 2. About the project (paste into "About the project")

Devpost renders Markdown here (headings, bold, lists, links, images by URL). Pasted into the
draft at 10:30 AM with paragraphs unwrapped (out of date now; paste again). No tables, and no text diagram: Devpost cut the
wide diagram off, so the architecture image stands in for it (the README keeps both).

---

**Code:** [github.com/Parth-Bansal30/e8-hackathon](https://github.com/Parth-Bansal30/e8-hackathon).
**Judged version:** tag [`v1.0-judged`](https://github.com/Parth-Bansal30/e8-hackathon/tree/v1.0-judged) (commit `32177ea`). Build and run steps are in the README.

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
  others), and make the sound 8 times. Each example is checked ("too quiet", "clipped", "loud
  room") and shown with its level over the room. An example unlike the others is flagged, and UP
  makes it again. A sound too close to one it already knows gets "Sounds like …". A test match
  follows, then Save. There's no laptop and no retraining: teaching builds a profile from the NPU's
  embeddings, which takes milliseconds.
- **Remembers them:** taught sounds are saved to the board's 64 MB OSPI flash in the background
  without dropping audio, and come back at boot in 2.2 ms.
- **Feels the knock:** the BMI323 accelerometer, read at 800 Hz, tells a knock on the table it sits
  on ("Felt here + heard") from a knock it only hears, like one on the TV ("Heard only (TV?)").
  A heard-only knock shows as a banner, not a full-screen alert.
- **Accessible alerts:** a full-screen card with icon, word and time, in a colour-blind-safe
  palette, plus an LED pattern for each sound. There are three urgency levels. Urgent sounds
  (glass breaking, siren) strobe once a second and stay on screen until you press; after 30 s
  with no response the strobe doubles to 2 flashes a second (still under 3 Hz, the
  photosensitivity limit) and the screen says how long it has waited. Low confidence shows as
  "Maybe: Dog barking?", never as a certain alert. A clear built-in match is on screen a median
  437 ms after the sound starts.
- **Timeline** of today's sounds, with repeats merged and "felt here" marked.
- **Ongoing sounds:** repeats merge into one entry with a count and how long it went on ("9 times,
  for 2 min, still sounding"). DOWN on an alert snoozes that sound for 10 minutes (it's still
  logged).
- **Someone talking nearby:** the home screen says so while speech is heard.
- **Learns from its mistakes:** on an alert or the timeline, LEFT means wrong and RIGHT means
  right. A wrong sound is kept as a counter-example, so a sound closer to it than to the profile
  is vetoed (kept in flash for taught sounds). Right on a taught sound adds an example and rebuilds
  its profile and threshold.
- **"What was that?":** a loud sound that matches nothing gets a soft banner and a timeline entry.
  Press UP and teaching starts with that sound as the first example.
- **Settings on the board** (DOWN): night mode (the screen goes dark; info sounds are only logged,
  important and urgent ones light it up), microphones off, a privacy note, sensitivity on the
  knob, **Sounds** (each built-in on or off; each taught sound set to Info, Important or Urgent, or
  renamed; saved in flash), **Calibrate room** (10 s, then shows the room level and how loud a sound
  must be), and forgetting one or all taught sounds. No app and no account.
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

![Architecture](https://raw.githubusercontent.com/Parth-Bansal30/e8-hackathon/main/docs/demo/gallery/02_architecture.png)

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

![Sound Sense running on the DK-E8: home screen and live Stats page](https://raw.githubusercontent.com/Parth-Bansal30/e8-hackathon/main/docs/demo/gallery/09_on_the_board.png)

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
- **Alert delay:** a clear built-in match is on screen a **median 437 ms** after the sound starts
  (403-490 ms; 18 of 30 recognised sounds in the live run, 0 wrong). Everything else is decided
  0.8 s after the onset, so the whole sound is in the window: about 0.87 s. Drawing the alert takes
  18.4-19.5 ms on the bench.
- **Display:** live view at 10.0 fps. With the urgent strobe plus 10 forced full redraws a second
  for 12 s, 0 audio blocks dropped.
- **Flash save:** 223-266 ms (median 252 ms) over 101 back-to-back saves of a full record; 1,000
  more saves had a longest of 304 ms. Across all 1,101 saves: 0 audio blocks dropped and 0 failed
  read-backs. Restore at boot: 2.2 ms.
- **Knock felt:** 21 of 22 knocks at 20 cm. IMU FIFO at 805 samples a second, 0 missed.
- **Few-shot (offline, 10 classes never trained on):** 5-way 5-shot 75.8% int8 (75.9% float);
  10-way 63.0%.
- **Taught sounds:** offline, at 8 examples, 45% accepted and 77% of untaught sounds rejected (38%
  accepted at 5 examples), on recordings the encoder never heard. Live, a Doorbell taught from the
  laptop was recognised in 4 of 5 trials, with no confusions with Siren.
- **Built-in sounds:** 88% offline with 4% false accepts. Live, from a laptop speaker on clips never
  used in training: 54% (alarm 75%).
- **Boot to Ready:** 1.5 s, self-test included.
- **Memory:** ITCM 97.9% (all hot code); MRAM 1.06 MB of 2 MB (model, fonts, cold code).
- **Tests on the flashed build:** full-app smoke test 16 of 16 and feature test 18 of 18 (no sound,
  driven over J-Link), 0 audio blocks dropped. A screen tour reached all 30 screens and states.
  Checked by eye and hand: night mode turns the backlight off after 15 s, and the knob moves the
  sensitivity with an on-screen bar.

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
- **ITCM full.** All the fast code lives in 256 KB of ITCM, now 97.9% full. Boot-only and rare
  code, including the flash driver, moved to MRAM with a section attribute and linker patterns.
- **Felt vs heard.** The IMU had to run at 800 Hz through its FIFO to catch a knock. On our table,
  knocks are felt within about 20 cm (21 of 22) but not at 50 cm (0 of 20), so the instructions say
  "place me on the table by the door". Pairing a knock with its sound also exposed a timing bug: our
  computed IMU frame times jumped back by up to 54 ms while the sensor's own clock ran on smoothly.
  Frames are now timed from the BMI323's clock (we haven't re-measured the pairing since). A
  smaller jump of up to 3 ms was the debugger: detaching J-Link stopped the CPU's cycle counter, so
  it's now re-armed every millisecond.
- **Alerting sooner without guessing.** Simply deciding at 0.45 s instead of 0.8 s cost about 9
  points of built-in recognition offline. Instead the board checks twice: at 0.4 s it alerts only
  if a built-in match is clear by a margin, otherwise it waits for the whole sound. Offline that
  alerted 55% of built-ins early at no loss; live it was 18 of 30, with 0 wrong early alerts.
- **A speaker isn't a dog.** Built-in sounds played from a laptop speaker in an unfamiliar room were
  recognised 54% of the time, against 88% offline. We tried re-measuring the built-in profiles on
  the board through the speaker, but the clips in each class varied so much that every threshold
  fell to its floor and wrong alerts went up, so we kept the trained profiles. That's why teaching
  your own sounds, with your own sources in your own room, became the centre of the product.

## Accomplishments that we're proud of

- The whole product runs on the board end to end: listen, recognise, teach, alert, save to flash,
  self-test at boot. All 16 of our must-have features are done, plus 14 of the 16 features on
  our "makes it good" list.
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

- Higher acceptance for taught sounds: an encoder trained for few-shot matching, not only
  classification.
- An LED colour per sound, chosen in Settings > Sounds.
- Dimming the screen in night mode instead of switching the backlight off (needs PWM).
- Smoke vs CO alarm from the beep pattern.
- An always-on listener on the M55-HE core that wakes the main core.
- Left/right direction from the two mics (the 8 cm spacing measured well enough to tell them
  apart).
- A relay output for a bed shaker, the standard night alert for deaf people.

## Known limits

- Built-in sounds from a laptop speaker: 54% live vs 88% offline. Teaching your own sounds is the
  strong path.
- Taught sounds are borderline offline: 45% accepted at 8 examples (38% at 5), 77% of untaught
  sounds rejected. Live, with the same doorbell or keys each time, the examples agree far more
  closely (4 of 5 trials).
- Knocks are felt within about 20 cm of the board.
- Teaching quality matters. Examples that clip or come too close together save a loose profile
  (threshold 0.50) that sits close to unrelated sounds: in the rehearsal, Keys taught that way was the
  best match for several knocks. A sound played much louder than when it was taught can also fall
  below its threshold (the Doorbell taught at volume 44 was only "Maybe" at volume 100, where the laptop
  clips). The teach screen already flags clipped and "loud room" examples.
- Only clear built-in matches meet our 0.5 s alert target (median 437 ms). Taught sounds, "Maybe"
  matches and anything unclear wait for the whole sound: about 0.87 s.

## Built with

Alif Ensemble E8 DevKit (Cortex-M55 + Ethos-U85-256), ExecuTorch with the Arm Ethos-U backend,
PyTorch (PT2E quantization), Vela, CMSIS-Toolbox, Arm GNU toolchain, Alif SETOOLS, SEGGER J-Link
and RTT, Python (NumPy, SciPy, Pillow). Training data: ESC-50 (CC BY-NC 3.0) and Speech Commands v2
(CC BY 4.0). The firmware builds on alifsemi/alif_vscode-template, and the ExecuTorch runner
started from Arm's alif-ethos-u85-npu-mnist learning path.

---

## 3. Before you submit (owner)

- [x] Sign in to Devpost and read the form (10:20 AM; no hackathon-specific questions).
- [x] Decide `main` and the judged version (tag `v1.0-judged` at the end, then merge again).
- [x] Tag `v1.0-judged` = `32177ea` (12:45 PM), merge `gates` into `main` (PRs #2 and #3), and
      replace `<hash>` in section 1 and at the top of "About the project" (1:19 PM).
- [x] Re-check against `gates`: the draft now covers the final firmware (8 examples, teach checks,
      437 ms alerts, Sounds page, Calibrate room, snooze, "still sounding", talking nearby).
- [ ] Merge PR #4 first: the two images in section 2 load from `main` (architecture, board photos).
- [ ] **Paste section 2 into the Devpost draft again:** the 10:30 AM paste is out of date (5
      examples, 0.85 s alerts, old feature counts). Same for the elevator pitch.
- [x] Hand checks (knob, night-mode backlight) passed at 1:25 PM; "Known limits" updated.
- [ ] Record the video, upload it to YouTube or Vimeo, and paste the link.
- [ ] Upload the gallery images (thumbnail first).
- [ ] Add the team members (team HashMap) in step 1 of the form.
- [ ] Check the preview: the code block and the bold text render.
- [ ] Submit before 4:30 PM (the page's hard deadline is 5:00 PM PDT).
