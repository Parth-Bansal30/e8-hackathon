# DK-E8: what we have observed

Every hardware and software fact we know about the Alif Ensemble E8 DevKit, and how we know
it. This is the reference for picking the hackathon project. Snapshot: 2026-10-18, board
AE822FA0E5597LS0, chip revision A0.

**How each fact is marked**

| Mark | Meaning |
|---|---|
| ✅ | Measured or seen working on the board |
| 📄 | From Alif's board files or product information only, not checked. The board files were wrong about both external memories, so don't rely on these without testing |
| ❔ | On the board, but not tested yet |
| ❌ | Not available on this kit |

Problems and their fixes are in [bringup-notes.md](bringup-notes.md).

---

## 1. The short version

| | What the kit has |
|---|---|
| **Inputs** | 2 PDM mics (16 kHz) ✅, BMI323 IMU ✅, touchscreen ✅, 5-way joystick ✅, knob ✅. Second IMU ❔. **No camera** ❌ |
| **Outputs** | 480x800 colour screen ✅, 2 RGB LEDs ❔. **No sound output** found ❌. No radio ❌ |
| **AI compute** | Ethos-U85-256 NPU ✅: small int8 CNNs take **0.3–0.45 ms**. Two Ethos-U55 NPUs and the M55-HE core ❔ |
| **CPU** | Cortex-M55-HP at **400 MHz** ✅ |
| **Memory** | MRAM 5.5 MB ✅, SRAM 9.75 MB ✅, plus **64 MB flash** ✅ and **64 MB PSRAM** ✅ (CPU access only so far) |
| **Biggest software limit** | With the prebuilt runtime, models must be int8 and run **entirely on the NPU**. Convolution and linear layers are proven; transformer layers are untested |

---

## 2. Processors

| Unit | What we know | Status |
|---|---|---|
| **Cortex-M55 HP** | 400 MHz (`SystemCoreClock`), D-cache on, Helium vector unit | ✅ |
| **Ethos-U85 NPU** | 256 MACs per cycle (compile target `ethos-u85-256`). Registers at `0x49042000` | ✅ |
| Cortex-M55 HE | Low-power core with its own 2 MB MRAM partition and TCMs. The CMSIS solution has an `E8-HE` build type | ❔ |
| 2x Ethos-U55 NPUs | In the chip. Would need a separate runtime build and separately compiled models | ❔ |
| Cortex-A32 cores | Alif lists them for the E6/E8 parts. The M55 toolchain doesn't target them | 📄 |

---

## 3. NPU performance (measured)

| Network | Size | Time per inference |
|---|---|---|
| Small int8 CNN, 28x28 single-channel input (MNIST-sized) | 105,696-byte compiled model | **304 µs** (121,802 cycles at 400 MHz) |
| Depthwise-separable int8 CNN, 49x40 input | 23,756 parameters, 36,064-byte compiled model | **448–454 µs** on the NPU, 483–488 µs including the ExecuTorch call |

- **ExecuTorch overhead per call:** about 35 µs, even when the program is reloaded on every call.
- **Vela's estimates are optimistic:** it predicted 0.36 ms for the second network, which measured 0.45 ms.
- Every operator in both networks ran on the NPU.
- All figures come from the DWT cycle counter in debug builds. Release builds haven't been timed.

---

## 4. Memory

| Memory | Size | Address | CPU speed (D-cache on) | Status |
|---|---|---|---|---|
| **MRAM** (on-chip, non-volatile) | 5.5 MB in total. Alif's default layout gives each core a **2 MB partition**: HE at `0x80000000`, HP at `0x80200000` | `0x80000000` | 201 MB/s sequential read | ✅ |
| **SRAM0 + SRAM1** | 4 MB + 4 MB, usable as one 8 MB block. **Off at boot**; software must power them up | `0x02000000` | not measured | ✅ |
| HP ITCM / DTCM | 256 KB / 1 MB | `0x0` / `0x20000000` | not measured | ✅ |
| HE ITCM / DTCM | 256 KB / 256 KB | | | ❔ |
| **OSPI flash** (ISSI octal NOR) | **64 MB** (the pack says 32 MB). Read-only when memory-mapped | `0xC0000000` | 74 MB/s sequential, 469 ns random 4-byte read | ✅ |
| **OSPI PSRAM** (AP Memory APS512XXN) | **64 MB** (the pack says 32 MB of HyperRAM). Read/write, x8 mode | `0xA0000000` | 107 MB/s read, 113 MB/s write (memcpy), 347 ns random read | ✅ |

- **Writing the flash:** 4 KB erase 82 ms, program 1.1 MB/s, driver-mode read 9.2 MB/s. Writing 15 MB takes about 15 s.
- **NPU access to the flash and PSRAM is untested.** Only the CPU has read them. This decides whether big models can run on the NPU.
- The PSRAM is volatile: its contents are lost when the power goes.
- x16 PSRAM mode would be about twice as fast, but it hangs the bus. Not investigated.
- **Fixed costs to plan for:**
  - A 480x800 RGB888 frame is **1.15 MB**, and tear-free drawing needs two, so 2.3 MB of SRAM. RGB565 would halve that (untested).
  - The ExecuTorch runtime needs SRAM pools for the method, scratch and tensor arena. Vela reports how big a model's arena must be, so the pools can be sized to the model.

---

## 5. Inputs

| Input | Details | Status |
|---|---|---|
| **PDM microphones** | A pair, PDM channels 4 and 5, captured interleaved at **16 kHz** (decimation 64). Use gains around 0x800 / 0xBB0: Alif's demo values are about 44 dB too quiet. Quiet room about −53 dBFS; speech −50 to −24 dBFS; a clap peaks around −12 dBFS. Good enough for live speech recognition. Mic spacing unknown | ✅ |
| **BMI323 IMU** | Accelerometer + gyroscope + temperature, over I3C. Reads about 1 g at rest. Alif's driver sets the **accelerometer to 50 Hz, ±16 g** and the gyro to 800 Hz, ±2000 °/s, and is **polled one sample at a time**. The chip can sample faster (up to 6.4 kHz per its datasheet), but that needs a modified driver, and its FIFO is untested | ✅ (50 Hz) |
| ICM-42670P IMU | Second IMU on the same I3C bus. Only one of Alif's IMU drivers can be linked at a time (duplicate symbols). The interrupt pins don't match between Alif's files, so poll | ❔ |
| **Touchscreen** | GT911 on I2C1. Coordinates equal screen pixels. Single touch tested. It only reports when something changes, so 150 ms with no report means the finger lifted | ✅ |
| **Joystick** | 5-way, on LPGPIO15 pins 0–4, active low | ✅ |
| **Knob** | Blue potentiometer **R79** beside the joystick. 12-bit ADC (0–4095 across its full travel), 0–1.8 V | ✅ |
| Camera | Two sockets (J22 top, J16 underside) and drivers for ARX3A0 and AR0144 sensors, but **no camera module in the kit** | ❌ |
| I2S microphone input (I2S3), low-power PDM | Listed in the board files | 📄 |

---

## 6. Outputs

| Output | Details | Status |
|---|---|---|
| **Display** | ILI9806E, **480x800**, MIPI DSI, RGB888 with bytes in B, G, R order. Draw into a back buffer and copy, or the image tears. CPU drawing is slow in unoptimised builds: a full redraw cycle measured about **135 ms** at -O0. Refresh rate and release-build drawing speed not measured | ✅ |
| RGB LEDs | Two, LED0 and LED1. Not yet confirmed by eye | ❔ |
| Sound | No codec, amplifier or speaker in the board files. A low-power I2S output pin exists, but the board files mark that peripheral as disabled | ❌ |
| Radio (Wi-Fi, Bluetooth) | None listed | ❌ |

---

## 7. Other interfaces (board files only, none tested)

| Interface | Board files say |
|---|---|
| Ethernet | RTL8201 PHY |
| USB | device role |
| SD card | slot present |
| CAN-FD | transceiver present |
| mikroBUS ("click board") socket | I2C, UART, SPI and PWM |
| I3C pin header | present |
| Analog | 24-bit ADC, comparators |

All 📄. The board connects to the laptop through the **PRG USB** port (J3), which carries
power, flashing (SETOOLS) and logging (J-Link RTT).

---

## 8. Software stack and its limits

### Versions (all ✅, details in [HANDOFF.md](HANDOFF.md))
- CMSIS-Toolbox 2.15.0, Arm GNU 13.3.1, Alif pack 2.0.4, SETOOLS 1.112 (with the A0 revision fix), J-Link V9.86a.
- On the board: a prebuilt **ExecuTorch 1.0** runtime from Arm's education repo.
- On the host: Python 3.12, torch 2.9.0, executorch 1.0.0, Vela 4.4.1.

### Model pipeline (✅ proven end to end)
1. PyTorch model.
2. int8 quantisation (`EthosUQuantizer`, calibrated on sample data).
3. `EthosUPartitioner` + Vela, targeting `ethos-u85-256` with system config `Ethos_U85_SYS_DRAM_Mid` and memory mode `Shared_Sram`.
4. The `.pte` file is compiled into the firmware as a C array, so it sits in MRAM.

### Limits that shape any project
| Limit | Detail |
|---|---|
| **int8 only, inputs and outputs too** | The prebuilt runtime's CPU quantize and dequantize ops give wrong results. Export with int8 inputs and outputs and do the conversion in C, using the scale and zero-point from export |
| **Everything must run on the NPU** | Proven: convolution, depthwise convolution, linear layers, ReLU, pooling, and batch norm folded into convolutions. The prebuilt runtime also contains CPU versions of many more ops (softmax, layer norm, embedding, GELU, batched matmul). Their registrations are only kept if the firmware links that library with `--whole-archive`. CPU fallback is untested |
| **Transformers** | The U85 supports transformer operators according to Arm, and Arm has shown a story-writing language model on the E8. Not yet compiled or run with this toolchain |
| **NPU integration needs three fixes** | Power SRAM0/1, override the Ethos-U driver's empty cache-maintenance hooks, and remap DTCM addresses for the NPU (`LocalToGlobal`). Details in [bringup-notes.md](bringup-notes.md) |
| **Model size in MRAM** | About 1.5 MB of weights fits beside the code in the default 2 MB HP partition. Repartitioning MRAM gives about 4–5 MB. Beyond that means the OSPI memories |
| **Loading the OSPI flash** | SETOOLS writes MRAM only. Getting data into the OSPI flash needs custom loader firmware that uses Alif's flash driver |
| **One IMU driver per build** | Duplicate symbols. Using both IMUs means writing a driver for one of them |
| **C only for some drivers** | `Driver_ADC.h` and `Driver_PDM.h` don't compile as C++ |
| **Build and flash** | 10–40 s per build and flash. A failed flash can mean the 130-second MRAM recovery |

### Host machines
- Reference laptop: MacBook Air M2, 8 GB, with PyTorch on its GPU (MPS).
- A second laptop with the same tool setup ([HANDOFF.md](HANDOFF.md)).

---

## 9. Not tested yet, most relevant first

1. **Can the NPU read weights from the OSPI flash or PSRAM, and how fast?** This decides the largest model that can run on the NPU.
2. **Fast IMU sampling** (hundreds of Hz to 6.4 kHz, FIFO). Needed for any vibration, knock or tap sensing.
3. **Transformer layers on the U85** with this ExecuTorch and Vela version.
4. **Release-build speed**, for drawing and signal processing.
5. The M55-HE core and the two Ethos-U55s, which would give a low-power always-on tier.
6. The second IMU, the LEDs (by eye), the I2S mic, and x16 PSRAM mode.
7. Ethernet, USB, SD card and CAN. Probably irrelevant.

---

## 10. What this means for choosing the project

1. **Inputs are sound, motion, touch, joystick and the knob.** The only output judges will see is the screen, plus two LEDs: no camera, no speaker, no radio.
2. **The NPU has a lot of headroom.** Small CNNs take under half a millisecond, so a project can afford several models, much bigger models, or models run many times a second.
3. **Pick model types that are already proven:** int8 convolutional and linear networks run entirely on the NPU. Transformers and LSTMs are a risk until tested.
4. **Small models (under 1.5 MB) need no extra work.** Bigger ones need the MRAM repartition (up to about 4–5 MB) or the external memory. External memory reads at about 74–107 MB/s, so each pass costs about 10–14 ms per MB of weights.
5. **SRAM is the tight resource:** two RGB888 frame buffers take 2.3 MB of the 8 MB, and the tensor arena needs the rest. RGB565 or moving buffers to PSRAM frees space.
6. **Motion detail needs the IMU driver changed:** 50 Hz is enough for tilt and gestures, not for knocks or vibration.
7. **Smooth animation needs work:** an unoptimised full redraw takes about 135 ms. Use release builds and redraw only what changes.
8. **Speed can be measured, power can't:** latency, throughput and duty cycle come from the cycle counter. There's no current-measurement setup, so power claims would be estimates.
