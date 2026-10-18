# Sound Sense gate results (2026-10-18, in progress)

| Test | Result | Key numbers | Notes |
|---|---|---|---|
| G1 encoder | pending | | |
| G2 IMU | pending | | |
| G3 live | pending | | |
| C1 NPU | pending | | |
| C2 flash | pending | | |
| C3 UI | pending | | |
| C4 mics | Direction probably feasible; which way the mic pair points is not settled | Spacing 8 cm (ruler), so at most 3.7 samples of lag. Measured lags (+ = mic 5 later): A −1.0, middle +2.2, B −1.7 samples, each within ±0.3. Front-clap level mic5 − mic4 −4.1 dB | See [C4 details](#c4-details) |
| C5 duty | pending | | |

## C4 details

**Recordings:** [c4_take1.wav](gates/audio/c4_take1.wav) has 16 claps at 1 m: 5 past mic hole A, 6 at the middle position, 5 past hole B, plus the start and stop joystick clicks. [c4_take2.wav](gates/audio/c4_take2.wav) has 5 key jingles. Both came out of the PSRAM over J-Link at 64 KB/s. Analysis: `tools/mic_geometry.py`, written to [c4_mic_geometry.json](gates/c4_mic_geometry.json).

**Mic positions:** the DK-E8 user guide (Figures 2A/2B) puts the two microphones on the underside, one at the middle of each long edge, each with a sound hole through the board. The ruler gives 8 cm between them.

**Lags:** each position gives a repeatable lag, but the pattern doesn't match that layout:
- If the mics were on the A–B line with no timing offset, A and B would show opposite lags of about ±3.7 samples and the middle position about 0.
- Instead A and B are both negative (−1.0 and −1.7) and the middle position is +2.2.
- One explanation fits every number: the pair points toward the middle position, and channel 5 runs about 1.35 samples (84 µs) ahead of channel 4. The middle position is then 3.55 samples from the offset, close to the 3.7 maximum for 8 cm. A and B sit near broadside, at ±0.35.
- The two channels' PDM phase settings differ (`0x1F` and `0x03` in `kws.c`). That alone is too small to explain 84 µs, so the offset is a hypothesis.
- **To settle it:** confirm where the person stood for each group, and record one set of claps from the far side of the middle position. The prediction there is about −4.9 samples.

**Levels:** mic 5 reads 4.1 dB below mic 4 on the middle claps (median), although its gain register is higher (`0xBB0` against `0x800`, +3.3 dB). Its acoustic path is therefore about 7 dB less sensitive.

**Clipping at about 1 m:** at the current gains, 4 of 16 claps and 6 of 10 key onsets reach full scale. Teaching has to flag clipped examples.

**Keys teach preview** (`tools/teach_preview.py`, float encoder ch128; results in [c4_keys_teach_preview.json](gates/c4_keys_teach_preview.json)):
- A profile built from the first 5 key onsets gets a threshold of 0.807.
- All 10 key onsets score 0.85 to 0.97 and are accepted. The claps (0.16 to 0.36) and the room background (−0.06 to 0.16) are all rejected.
- **Built-in false alerts, before anything is taught:** 7 of 10 key onsets clear the "water" threshold, and 3 of 16 claps clear "glass". G3 should count these.
