# Sound Sense: product spec

Working title for the hackathon project. A home sound monitor for people who are deaf or hard of
hearing. It runs on the DK-E8, recognises everyday sounds, learns your own sounds from a few
examples, and alerts on the screen and LEDs. Audio is never stored and never leaves the device.

The go/no-go tests for this spec are in [HANDOFF-gates.md](HANDOFF-gates.md). The hardware facts
behind it are in [HARDWARE.md](HARDWARE.md).

Snapshot: 2026-10-18. Status: built and running on the board; what is done is in [STATUS.md](STATUS.md).

---

## 1. Who it's for and what it must get right

| Person | Situation | What they need from it |
|---|---|---|
| Deaf or hard-of-hearing adult at home | Misses the doorbell, a knock, the smoke alarm, the kettle, the washer finishing | Notice the sounds that matter, ignore the rest, and never miss an alarm |
| Parent with hearing loss | Baby in another room | Baby crying alerts that work at night, with the screen dark |
| Older person with age-related hearing loss | Doesn't want a phone app | A box that works when plugged in, with nothing to configure |
| Hearing family member | Sets it up | Teaching the household's own sounds in a minute |
| Anyone wearing noise-cancelling headphones or earplugs | Focus or sleep | The same alerts, temporarily |

**Design rules**

1. **Glanceable.** Icon + word + time on every alert. Never colour alone, and use a palette that
   works with colour blindness.
2. **Alarms can't be missed.** Urgent sounds use a full-screen strobe and the LEDs, and repeat
   until acknowledged.
3. **Few false alarms.** A box that cries wolf gets unplugged. Every feature that cuts false
   alarms (rejecting unknown sounds, felt vs heard, fixing mistakes) is high priority.
4. **Private by design.** Nothing recorded, nothing sent. Say so on screen.
5. **Honest when unsure.** Low confidence shows as "Maybe: doorbell?", not as a certain alert.
6. **Fails loudly.** A boot self-test says whether the mics, NPU and saved sounds are OK.

What phones already do: iOS Sound Recognition can learn a custom doorbell or appliance. What this
does that a phone can't:
- It is always on, with no phone, account or network.
- It can tell a knock *on the surface it sits on* (felt by the IMU) from a knock sound on the TV
  (heard only).
- It is a dedicated, always-visible light signal, like the flashing doorbell lights many deaf
  households already use.

---

## 2. Feature catalogue

Priorities:
- **P0:** the product and the demo don't work without it.
- **P1:** large user value or a large rubric gain; build once P0 is solid.
- **P2:** polish, if there's time.
- **P3:** stretch goal, or depends on hardware nobody has tested.

**The column headed "Gate"** names the test in [HANDOFF-gates.md](HANDOFF-gates.md) that decides
whether the feature is feasible. G1–G3 are go/no-go gates; C1–C5 are build checks.

### Hearing

| ID | Feature | Why it matters | Gate | Priority |
|---|---|---|---|---|
| H1 | Built-in sounds: knock, dog barking, baby crying, alarm beeping, glass breaking, running water, siren, speech | Useful out of the box | G1, G3 | **P0** |
| H2 | Teach your own sound from 5 examples (doorbell, microwave, kettle, washer chime, intercom, ringtone) | Every home's doorbell is different; this is the core feature | G1, G3 | **P0** |
| H3 | Reject unknown sounds instead of forcing them into the nearest class | Without it, every new sound becomes a false alarm | G1, G3 | **P0** |
| H4 | Smoke vs CO alarm from the beep pattern (smoke: 3 beeps then a pause; CO: 4 beeps then a pause; both standardised) | Life safety; tells the user *which* alarm | none (CPU timing on alarm-band energy) | P1 |
| H5 | Ongoing sounds: "water running for 6 min", "dog barking for 3 min", "alarm still sounding" | Tells a tap left on from a short splash | none | P1 |
| H6 | "What was that?": a loud sound it doesn't know gives a soft alert with one-tap *teach this* | Safety net; turns surprises into taught sounds | G3 | P1 |
| H7 | Your name being called, taught from family members saying it | "Someone is calling you" is a top request from deaf users | G3 (taught speech) | P1 |
| H8 | "Someone is talking nearby" indicator (not an alert) | Awareness of conversation in the room | G1 | P2 |
| H9 | Noise meter: live level, loud-noise warning, a sparkline of the day | Hearing protection; sensory sensitivity | none | P2 |
| H10 | Teach from one long recording: auto-split 20 s with 5 doorbell rings | Faster teaching | none | P2 |
| H11 | Improve a built-in sound with your own examples ("my dog's bark") | Personalises built-ins | G1 | P2 |
| H12 | Direction of the sound (left / right) from the two mics | "The sound came from the kitchen side" | C4 (mic spacing) | P3 |

### Felt (IMU fusion)

| ID | Feature | Why it matters | Gate | Priority |
|---|---|---|---|---|
| F1 | Knock **felt and heard** (on this door or table) vs **heard only** (TV, video) | The main false-alarm reducer, and the showcase of sensor fusion | G2 | **P1** |
| F2 | Taught sounds also store their vibration signature (door slam, washer when the box sits on it) | Better accuracy for sounds that shake the surface | G2 | P2 |
| F3 | Device moved: re-measure the noise floor and remind about placement | Keeps accuracy after someone moves it | none (50 Hz IMU is enough) | P2 |
| F4 | Shake or double-tap the box to dismiss an alert | No need to aim at the screen; good for limited dexterity | G2 | P2 |
| F5 | Vibration-only events: washer stopped vibrating, strong shaking | Appliance "done" without a chime; earthquake awareness | G2 | P3 |

### Alerts

| ID | Feature | Why it matters | Gate | Priority |
|---|---|---|---|---|
| A1 | Alert card: big icon, word, time, confidence; colour by category | Readable from across the room | C3 | **P0** |
| A2 | LED pattern per sound (colour + rhythm) | Visible when the screen isn't in view, like doorbell flashers | C3 (LEDs never confirmed by eye) | **P0** |
| A3 | Three urgency levels. **Urgent** (smoke/CO, glass, baby): full-screen strobe + LED strobe. **Important** (doorbell, knock, name): full-screen card. **Info** (microwave, water, dog): banner + timeline | Matches how much attention each sound deserves | C3 | **P0** |
| A4 | Acknowledge: tap anywhere or press the joystick | Simple | none | **P0** |
| A5 | Urgent alerts repeat until acknowledged, and escalate after 30 s (bigger, brighter, faster) | Alarms can't be missed | none | P1 |
| A6 | Night mode: dim screen, only urgent and chosen sounds, strobe for urgent | Bedroom use, baby at night | C3 (backlight control) | P1 |
| A7 | "Maybe: doorbell?" style for low-confidence matches | Honest when unsure | none | P1 |
| A8 | Snooze one sound for 30 min | "The dog won't stop" | none | P2 |
| A9 | Modes: Home / Night / Away (log only) / Baby | One switch for the situation | none | P2 |

### Teaching and personalisation

| ID | Feature | Why it matters | Gate | Priority |
|---|---|---|---|---|
| T1 | Guided teach flow: pick a preset name and icon → "make the sound 5 times" → progress ring → save | Anyone can teach a sound in under a minute | G3 | **P0** |
| T2 | Quality check on each example: too quiet, clipped, background too loud, unlike the others | Bad examples are the main cause of bad taught sounds | G3 | **P0** |
| T3 | Test right after teaching: "play it again" shows the match score | Builds trust, catches problems early | G3 | **P0** |
| T4 | Fix mistakes from the timeline. "Wrong" keeps it as a counter-example; "right" adds it as an example | Learns from use, cuts repeat false alarms | G1 | P1 |
| T5 | Per-sound sensitivity on the R79 knob, with a live match-score bar | Direct, physical tuning | none | P1 |
| T6 | Similar-sound warning while teaching: "sounds like Microwave: keep separate or merge?" | Prevents two sounds that can't be told apart | G1 | P1 |
| T7 | Manage sounds: rename, delete, add examples, set urgency and LED colour, turn built-ins on or off, factory reset | Basic control | none | P1 |

### Timeline and history

| ID | Feature | Why it matters | Gate | Priority |
|---|---|---|---|---|
| L1 | Today's timeline: icon, time, duration, repeat count, felt/heard | "What did I miss?" | none | **P0** |
| L2 | "While you were away" summary of unacknowledged events | Catching up after being out | none | P2 |
| L3 | Daily counts per sound and noise over the day | Patterns (dog barks when you're out) | none | P2 |
| L4 | Event detail: the 1 s spectrogram picture plus the top 3 guesses. Opt-in | "What was that sound?" without storing audio | none | P2 |
| L5 | Real clock: set once with the joystick, kept by the RTC (otherwise "12 min ago") | Real timestamps | none | P2 |
| L6 | Export the log over USB or SD card | Sharing with family or carers | untested hardware | P3 |

### Setup, trust and system

| ID | Feature | Why it matters | Gate | Priority |
|---|---|---|---|---|
| S1 | Boot self-test: both mics alive and levels sane, NPU output matches the stored reference, saved sounds valid → "Ready" | Fails loudly; trust | C1, C2 | **P0** |
| S2 | Saved sounds survive power loss: two copies with checksums; listening resumes seconds after power returns | Plug-and-forget | C2 | **P0** |
| S3 | Live "what I hear" view: scrolling spectrogram + top 3 matches | Transparency, teaching aid, great for the demo | C3 | **P0** |
| S4 | Stats page: NPU ms, alert latency, duty cycle, memory use | Evidence for the performance score | C1, C5 | **P0** |
| S5 | Noise-floor calibration (10 s) at first boot and on demand | Thresholds that suit the room | G3 | P1 |
| S6 | Privacy screen: "nothing is recorded or sent"; mic mute on a long joystick press with a clear indicator | Trust | none | P1 (screen) / P2 (mute) |
| S7 | First-run wizard: placement tips, choose sounds, test an alert | Self-service setup | none | P2 |
| S8 | Placement check for knock fusion: "knock on the door now" | Makes F1 work wherever it's mounted | G2 | P2 |

### Accessibility of the device itself

| ID | Feature | Why it matters | Gate | Priority |
|---|---|---|---|---|
| X1 | High contrast, large type and icons, icon + word everywhere, colour-blind-safe palette | Design rule | none | **P0** |
| X2 | Every action works by touch, joystick or knob | Motor accessibility | none | P2 |
| X3 | Flip the UI 180° from the IMU (wall or stand mounting) | Placement freedom | none | P2 |
| X4 | Screen brightness on the knob | Night use | C3 (backlight) | P2 |

### Efficiency

| ID | Feature | Why it matters | Gate | Priority |
|---|---|---|---|---|
| E1 | Run the NPU only when the sound level rises above the room's noise floor | Low duty cycle; the efficiency evidence | C5 | P1 |
| E2 | Sleep (WFI) between audio blocks. The current loop busy-polls | Same | C5 | P1 |
| E3 | Vectorised FFT (CMSIS-DSP) instead of the plain float FFT (1.2 ms per frame) | Faster features | C5 | P2 |
| E4 | Always-on tier on the M55-HE core (and a U55) that wakes the main core | The "architecture" story at its best | untested | P3 |

### Integration (hardware untested)

| ID | Feature | Why it matters | Priority |
|---|---|---|---|
| I1 | Send alerts to a PC or phone over USB or Ethernet | Reaches the user anywhere in the home | P3 |
| I2 | Drive a lamp flasher or bed shaker through a relay on the mikroBUS socket | Bed shakers are the standard night alert for deaf people | P3 |
| I3 | Log to the SD card | Long history | P3 |

---

## 3. Priority list (build order)

**P0: the working product (build in this order)**

1. **Listening pipeline:** the existing audio front end → the sound encoder on the NPU →
   matching against stored sound profiles on the CPU. Gives H1 and H3.
2. **Alert UI and LEDs:** A1, A2, A3 levels, A4.
3. **Teach flow:** T1, T2 and T3, which deliver H2.
4. **Saved sounds in flash:** S2, then the boot self-test S1.
5. **Timeline:** L1.
6. **Live view and stats page:** S3, S4.
7. **Design rule throughout:** X1.

**P1: the features that make it good (in order of value for effort)**

1. **E1 + E2:** sound-gated NPU and sleeping between audio blocks. Cheap, and it backs the
   performance numbers.
2. **F1 felt vs heard:** only if test G2 passes. It's the fusion headline.
3. **T4 fix mistakes, T5 knob sensitivity, A7 "maybe" style:** all cut false alarms.
4. **H4 smoke vs CO:** test it with generated beep patterns played from a phone.
5. **A5 repeat and escalate, A6 night mode.**
6. **H5 ongoing sounds, H6 "what was that?".**
7. **T6 similar-sound warning, T7 manage sounds, S5 calibration, S6 privacy screen, H7 name.**

**P2 (polish):** L2, L3, L4, L5, A8, A9, F2, F3, F4, H8, H9, H10, H11, S7, S8, X2, X3, X4, E3.

**P3 (stretch):** H12 direction (only if C4 shows the mics are far enough apart), E4 M55-HE tier,
F5, I1–I3, and distilling the encoder from an AudioSet model.

---

## 4. Architecture

```
2 PDM mics ─► M55: capture, log-mel (firmware/app/kws.c, bit-exact with Python)
                       │ every 100 ms on the latest 1 s; only when the sound level rises (E1)
                       ▼
             NPU: sound encoder (int8) ─► 64-d embedding
                       ▼
             M55: normalise, cosine match against all profiles
                  (built-in profiles + taught profiles + "background" + "speech")
                  → best match, its own threshold, "unknown" if below
BMI323 (≥800 Hz) ─► M55: vibration onset ─► felt + heard / heard only (F1)
                       ▼
     alerts: LCD + 2 RGB LEDs        profiles + settings ⇄ OSPI flash (copies A/B, CRC)
     input: touch, joystick, R79 knob
```

**Key design choices**

- **One kind of sound profile.** Built-in sounds and taught sounds are both profiles: a 64-d mean
  embedding with its own threshold. The built-in profiles come from the training data, and the
  taught ones from 5 examples on the device. Matching, rejecting unknowns, fixing mistakes and
  improving built-ins all work the same way. Built-in profiles can be re-measured on the board in
  minutes if the room or mics differ from the training data.
- **The NPU only produces the embedding.** The board runner reads a single output, and a 64-byte
  output is all that's needed. Matching about 20 profiles is 20 dot products on the CPU.
- **Thresholds come from the examples.** When a sound is taught, compute each example's similarity
  to the mean of the others. The threshold is the lowest of those minus a margin, clamped to
  [0.5, 0.85]. The knob (T5) moves it.
- **Saved sounds live in the OSPI flash**, not the PSRAM: writing the flash is proven, and the
  PSRAM loses its contents at power-off. Writes alternate between two copies, each with a sequence
  number and CRC; boot loads the newest valid one. A 4 KB erase blocks for 82 ms, and the audio
  ring holds 320 ms, so a save doesn't drop audio.
- **Driver conflict:** the pack's flash driver and the vendored PSRAM driver can't be linked
  into one project. The product uses the flash only. Recording training audio into PSRAM is a
  separate `recorder` firmware.

---

## 5. Data and models

| Use | Source | Notes |
|---|---|---|
| Built-in sounds, plus extra classes for training the encoder | **ESC-50** (2,000 clips × 5 s, 50 classes, CC BY-NC 3.0) | Resample 44.1 kHz → 16 kHz and take 1 s windows around the loudest part. Credit it in the README |
| Speech, and background noise | **Speech Commands v0.02** (already on the reference Mac) | 35 words give the encoder many extra classes to learn from; `_background_noise_` gives "background" |
| Our mics in our room | **Board recordings** (`firmware/recorder`) | Closes the gap between dataset microphones and our PDM mics |
| Smoke and CO alarms | **Generated** beep patterns | The beep timings are standardised; play them from a phone |

**Encoder:**
- **Shape:** DS-CNN-like, on the same 49x40 log-mel input as the keyword model, so the C feature
  code is reused unchanged. At most 300k parameters and 50 M MACs; 64-d output.
- **Training:** a cosine classifier over all training classes (ESC-50 classes plus Speech
  Commands words). This is known to give embeddings that work well for few-shot matching. Train
  with noise mixing, gain changes, time shifts and simple synthetic reverberation.
- **Held out:** 10 ESC-50 classes are never trained on, to measure how well it learns unseen sounds.

---

## 6. Screens (480x800 portrait)

| Screen | Contents |
|---|---|
| Home | "Listening" status, room level, live spectrogram strip, top 3 matches, last 3 events, mode |
| Alert | Full screen: icon, word, time, confidence, felt/heard tag; strobe for urgent; tap to dismiss |
| Timeline | Today's events, newest first; tap one for "right / wrong / teach this" |
| Teach | Preset picker → capture ring (5 dots, each with its quality result) → test → save |
| Sounds | List of built-in and taught sounds: on/off, urgency, LED colour, sensitivity (knob), delete |
| Stats | NPU ms per inference, alert latency, NPU and CPU duty cycle, memory map, model size |

---

## 7. Demo script (about 3 min)

1. **Ready:** boot shows the self-test, then "Ready".
2. **Built-in sounds:** play a dog bark and a baby cry from a phone; the alerts appear with their
   LED colours.
3. **Teach a sound:** choose "Doorbell" and play a chime 5 times; the 5 captures show their
   quality checks. Ring it again and it matches. A different chime is rejected.
4. **Felt vs heard:** knock on the table and it says **"Knock, felt here"**. Play a knock from
   the phone held in the air and it says **"Knock sound, heard only"**, as a low-priority alert.
5. **Fix a mistake:** tap a false alert and mark it "Wrong"; the same sound no longer triggers.
6. **Persistence:** unplug, plug back in, ring the doorbell: still recognised.
7. **Judge's turn:** hand the board to a judge to teach their own sound (keys, a ringtone).
8. **Numbers:** a smoke-alarm pattern gives the urgent strobe. Finish on the Stats page: alert
   latency, NPU time, duty cycle.

---

## 8. Rubric fit (expected 83/100; floor about 74, ceiling about 88)

| Criterion | Expected | Features that earn it |
|---|---|---|
| Functionality & reliability /20 | 17 | H3, T2, T4, S1, S2; phone playback makes the demo repeatable |
| Performance & efficiency /20 | 16 | S4, E1, E2, E3; alert latency, NPU ms, duty cycle |
| Effective use of hardware /15 | 13 | NPU encoder, CPU DSP and matching, flash for saved sounds, mics, IMU, touch, knob, LEDs; E4 → 15 |
| Multimodal & sensor fusion /15 | 13 | F1 (about 10 without it), F2, F4; H12 adds a point |
| Innovation & use-case value /10 | 8 | Accessibility, privacy, on-device learning; felt vs heard is the new part |
| Technical ambition /10 | 8 | Custom embedding model, learning from a few examples, fusion, persistence |
| Engineering & demo /10 | 8 | The judge teaches it a sound; self-test; architecture write-up |
