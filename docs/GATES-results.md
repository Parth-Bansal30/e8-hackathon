# Sound Sense gate results (2026-10-18, in progress)

| Test | Result | Key numbers | Notes |
|---|---|---|---|
| G1 encoder | SOFT (borderline) | 5-way 5-shot int8 75.8% (float 75.9%); 10-way 63.0%. Taught accepted 38%, untaught rejected 79%, AUROC 0.687. Built-ins 88% recognised with 4% false accepts. 219k params, 43.2 M MACs, .pte 240 KB | Shipped ch192 (e22234c); also tried ch128 (74.1%) and ch192 with SpecAugment (72.6%). Taught-sound acceptance is the weak spot. Details in `model/out/g1_metrics.json` |
| G2 IMU | SOFT | 805 to 834 Hz through the FIFO, 0 missed, 254 µs per read, 3.5% CPU. Felt at 20 cm: 21 of 22 knocks (95%). At 50 cm: 0 of 20 hard knocks; they peak at the sensor's noise level. Phone 30 cm above the table: 2 of 21 knock sounds flagged as felt, both while the phone was being put down; the raw data shows 0 of 21. Alignment: 55% within ±10 ms, held back by an IMU timestamp bug | F1 ships with "place me on the table by the door" (S8). The bug: computed IMU frame times jump back by up to 54 ms while the sensor clock stays continuous. Fix: time frames from the BMI323 sensor clock. Tools: `tools/g2_report.py`, `tools/g2_raw.py`; log in `gates/g2_rtt.log` |
| G3 live | pending | | |
| C1 NPU | pending | | |
| C2 flash | pending | | |
| C3 UI | Row 1 (LEDs) passes; rows 2 to 5 pending | LEDs: both RGB LEDs showed red, green, blue and white exactly as commanded, confirmed by eye at the board. Blinks at 2, 4 and 8 Hz are visible across the room | Pins as in `docs/bringup-notes.md`; check run by `firmware/imu_bench` at boot (`[C3]` lines in its RTT log) |
| C4 mics | GO for left/right direction (H12) | Spacing 8 cm (ruler). The Mac speaker in line with the pair gave +3.2 samples on the left and −3.6 on the right, steady to ±0.2. That makes the maximum lag 3.4 samples (73 mm) and the channel offset −0.2. Mic 4 is the left mic. Level mic5 − mic4: −4.1 dB on hand claps, 0 to +1 dB from the speaker | See [C4 details](#c4-details) |
| C5 duty | pending | | |

## C4 details

**Recordings:** [c4_take1.wav](gates/audio/c4_take1.wav) has 16 claps at 1 m: 5 at position A, 6 at the middle position (a short end, about 1 m from the left mic), 5 at position B, plus the start and stop joystick clicks. [c4_take2.wav](gates/audio/c4_take2.wav) has 5 key jingles. Both came out of the PSRAM over J-Link at 64 KB/s. Analysis: `tools/mic_geometry.py`, written to [c4_mic_geometry.json](gates/c4_mic_geometry.json).

**Mic spacing:** 8 cm by ruler.

**Lags** (positive means mic 5 hears it later):
- **Calibration with this Mac's speaker** (`tools/mic_calib.py`: 5 synthetic claps per run, results in [c4_mic_calib.json](gates/c4_mic_calib.json)). The laptop sat on the table in line with the pair, as far out as the cable allowed. Left end **+3.20** (per-clap spread 0.12); right end **−3.61** (spread 0.22). The channel offset, (left + right) / 2, is −0.20 samples (13 µs), so in practice there is none. The maximum lag, (left − right) / 2, is **3.40 samples**, i.e. 73 mm of path difference against 80 mm by ruler. Mic 4 is the left mic, as `docs/HANDOFF.md` says.
- **Hand claps** (take 1): middle position, at a short end about 1 m from the left mic, +2.2; positions A and B −1.0 and −1.7. The claps came from standing height, and the positions weren't exactly in line with or side-on to the pair, so these are angles off the line rather than calibration points. +2.2 corresponds to about 50° off the left end. An earlier note read them as a −1.35-sample channel offset; the speaker runs rule that out.
- **H12 (left/right):** GO. A lag of ±3.4 samples against about ±0.2 of spread at one position gives a usable left / middle / right reading. No per-board calibration is needed beyond the −0.2 offset.
- **Mic layout:** the DK-E8 user guide (Figures 2A/2B) shows one mic on the underside at the middle of each long edge. The measured pair points left–right as seen from the person's usual side, so check which edges those are on this board before writing direction labels into the UI.

**Levels:** with the Mac speaker in line with the pair, mic 5 reads 0 to +1 dB relative to mic 4; its gain register is higher (`0xBB0` against `0x800`, +3.3 dB). On the hand claps it read 4.1 dB below mic 4, so the level difference depends on direction and height. Don't use level for direction; the lag is far more reliable.

**Clipping at about 1 m:** at the current gains, 4 of 16 claps and 6 of 10 key onsets reach full scale. Teaching has to flag clipped examples.

**Keys teach preview** (`tools/teach_preview.py`, float encoder ch128; results in [c4_keys_teach_preview.json](gates/c4_keys_teach_preview.json)):
- A profile built from the first 5 key onsets gets a threshold of 0.807.
- All 10 key onsets score 0.85 to 0.97 and are accepted. The claps (0.16 to 0.36) and the room background (−0.06 to 0.16) are all rejected.
- **Built-in false alerts, before anything is taught:** 7 of 10 key onsets clear the "water" threshold, and 3 of 16 claps clear "glass". G3 should count these.
