# Sound Sense gate results (2026-10-18, in progress)

| Test | Result | Key numbers | Notes |
|---|---|---|---|
| G1 encoder | pending | | |
| G2 IMU | pending | | |
| G3 live | pending | | |
| C1 NPU | pending | | |
| C2 flash | pending | | |
| C3 UI | pending | | |
| C4 mics | Direction feasible; needs one calibration constant per board | Spacing 8 cm (ruler), so at most 3.7 samples of lag. Clapping 1 m off the left mic, in line with the pair, gave +2.2 samples; positions A and B to the side gave −1.0 and −1.7. That fits a constant offset between the channels of −1.35 samples and a measured maximum of 3.55. Mic 4 is the left mic. Front-clap level mic5 − mic4 −4.1 dB | See [C4 details](#c4-details); the right-side claps (expected about −4.9) are still to do |
| C5 duty | pending | | |

## C4 details

**Recordings:** [c4_take1.wav](gates/audio/c4_take1.wav) has 16 claps at 1 m: 5 at position A, 6 at the middle position (1 m off the left mic, in line with the pair), 5 at position B, plus the start and stop joystick clicks. [c4_take2.wav](gates/audio/c4_take2.wav) has 5 key jingles. Both came out of the PSRAM over J-Link at 64 KB/s. Analysis: `tools/mic_geometry.py`, written to [c4_mic_geometry.json](gates/c4_mic_geometry.json).

**Mic positions:** the DK-E8 user guide (Figures 2A/2B) puts the two microphones on the underside, one at the middle of each long edge, each with a sound hole through the board. The ruler gives 8 cm between them.

**Lags:** each position gives a repeatable lag (within ±0.3 samples). Positive means mic 5 hears it later.
- **Middle position** (6 claps): about 1 m off the *left* microphone, in line with the pair, as the person at the board confirmed. Lag +2.2, so mic 4 is the left mic, as `docs/HANDOFF.md` says.
- **A and B** (5 claps each): roughly to the side of the pair. Lags −1.0 and −1.7.
- **Reading:** taking −1.35 samples (84 µs) as a constant offset between the two channels fits all three positions. A and B then come out at ±0.35, close to broadside. The left end comes out at +3.55, which matches the 8 cm spacing (3.7 at most). Without an offset, the left end would mean 4.7 cm of spacing, which contradicts the ruler.
- The two channels' PDM phase settings differ (`0x1F` and `0x03` in `kws.c`). That is too small to cause 84 µs on its own, so where the offset comes from is open. It only needs measuring once per board.
- **Confirmation still to do:** 5 claps 1 m off the *right* mic, in line with the pair. The offset reading predicts about −4.9 samples; without an offset it would be about −2.2. The result also gives the offset directly, as (left + right) / 2, and the maximum lag, as (left − right) / 2.
- **H12 (left/right):** feasible. After subtracting the offset, the lag runs over about ±3.6 samples, against ±0.3 of spread at one position.

**Levels:** mic 5 reads 4.1 dB below mic 4 on the middle claps (median), although its gain register is higher (`0xBB0` against `0x800`, +3.3 dB). Its acoustic path is therefore about 7 dB less sensitive.

**Clipping at about 1 m:** at the current gains, 4 of 16 claps and 6 of 10 key onsets reach full scale. Teaching has to flag clipped examples.

**Keys teach preview** (`tools/teach_preview.py`, float encoder ch128; results in [c4_keys_teach_preview.json](gates/c4_keys_teach_preview.json)):
- A profile built from the first 5 key onsets gets a threshold of 0.807.
- All 10 key onsets score 0.85 to 0.97 and are accepted. The claps (0.16 to 0.36) and the room background (−0.06 to 0.16) are all rejected.
- **Built-in false alerts, before anything is taught:** 7 of 10 key onsets clear the "water" threshold, and 3 of 16 claps clear "glass". G3 should count these.
