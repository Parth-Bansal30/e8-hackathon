# Handoff: reproduce this setup on another machine

This is everything needed to rebuild the toolchain, flash the board and re-run every test on
a second laptop with the same Alif DK-E8 kit. It also records what was done, in order, and
where the work stopped. Board-specific problems and their fixes are in
[bringup-notes.md](bringup-notes.md); this file links to them rather than repeating them.

Snapshot date: 2026-10-18. Reference machine: MacBook Air M2 (8 GB), macOS 26.6.2, arm64.

---

## 1. Where things stand

| Area | State | Project / file |
|---|---|---|
| Build, flash (SETOOLS), RTT logging | working | `tools/build_flash.sh`, `tools/rtt_dump.sh` |
| MIPI DSI display 480x800 | working | `lcd_image` |
| ExecuTorch on Ethos-U85 (MNIST, int8) | working, 304 µs per inference | `mnist_executorch`, `npu_display` |
| Touch drawing -> NPU digit recognition | working | `npu_draw` |
| Peripheral self-test (joystick, pot R79, BMI323 IMU, mics, touch, NPU) | passing | `hw_test` |
| RGB LEDs | self-test written, not visually confirmed | `hw_test` |
| ICM-42670P IMU | not tested | |
| Camera | **no camera module in the kit**; capture test written for one | `cam_test` |
| Mic gain | fixed: 0x800 instead of Alif's demo value 0x0D (+44 dB) | `hw_test`, `app/kws.c` |
| **Final project: voice + motion controller** | keyword model trained; keyword app **built and flashed but not tested**, see §7 | `model/*kws*`, `firmware/app` |

---

## 2. Exact software versions

Install these versions. Newer ones may work but are untested.

### Host and board tools

| Tool | Version | Where it comes from |
|---|---|---|
| macOS | 26.6.2 (arm64) | any recent macOS on Apple silicon should work; Linux should too, untested |
| Homebrew | current | https://brew.sh |
| git | Homebrew current | `brew install git` |
| GitHub CLI `gh` | 2.102.0 | `brew install gh` |
| SEGGER J-Link | **V9.86a** | `brew install --cask segger-jlink` (the board's on-board J-Link OB) |
| Alif SETOOLS (Security Toolkit) | **1.112.00** (`app-release-exec-macos-SE_FW_1.112.00_DEV.tar`) | https://alifsemi.com/support/kits/ensemble-e8devkit/ (Alif account) |
| VS Code | current | only needed for the Arm Environment Manager, which installs the toolchain |

### Toolchain (vcpkg artifacts, installed by the Arm Environment Manager)

| Artifact | Version | vcpkg id |
|---|---|---|
| CMSIS-Toolbox (cbuild, csolution, cpackget) | **2.15.0** | `arm:tools/open-cmsis-pack/cmsis-toolbox` |
| Arm GNU Toolchain (arm-none-eabi-gcc) | **13.3.1** | `arm:compilers/arm/arm-none-eabi-gcc` |
| CMake | **3.31.12** | `arm:tools/kitware/cmake` |
| Ninja | **1.13.2** | `arm:tools/ninja-build/ninja` |

`firmware/vcpkg-configuration.json` asks for `^` ranges (`cmsis-toolbox ^2.12.0`,
`gcc ^13.2.1-0`, `cmake ^3.28.4`, `ninja ^1.12.0`), so a fresh install may resolve newer
versions. Check what you got (§3.3). If anything differs, change the entries in that file to
the exact versions above before you open the folder in VS Code.

### VS Code extensions

| Extension | Version |
|---|---|
| `arm.environment-manager` | 1.26.0 |
| `arm.cmsis-csolution` | 1.66.0 |
| `marus25.cortex-debug` | 1.12.1 |
| `ms-vscode.cpptools-extension-pack` | 1.5.1 (cpptools 1.34.4) |

### CMSIS packs (in `~/.cache/arm/packs`)

All of these are pinned in `firmware/alif.csolution.yml`:

| Pack | Version |
|---|---|
| `AlifSemiconductor::Ensemble` | **2.0.4** |
| `ARM::CMSIS` | **6.0.0** |
| `ARM::CMSIS-Compiler` | **2.1.0** |
| `Keil::MDK-Middleware` | **8.2.0** |
| `ARM::ethos-u-core-driver` | **1.26.5** |
| `ARM::CMSIS-View` | 1.2.0 (pulled in as a dependency) |

### Firmware sources

| Item | Version / pin |
|---|---|
| `firmware/` base | copied from `alifsemi/alif_vscode-template` @ `8b1aa0b09eacf68a28850af00c11f0b5af03c100` |
| ExecuTorch runtime for the board (`et_bundle`) | from `arm-education/alif-ethos-u85-npu-mnist`, sha256 `7e64d9bd…1cb2` (checked by `tools/fetch_et_bundle.sh`) |
| SEGGER RTT | V7.96h (vendored in `firmware/libs`) |

### Python model-export environment

| Package | Version |
|---|---|
| Python | **3.12.7** (Anaconda's `/opt/anaconda3/bin/python3`; any 3.12 should do) |
| torch / torchvision / torchao | **2.9.0 / 0.24.0 / 0.14.0** |
| executorch | **1.0.0** (source tag v1.0.0 = `8c84780911e7`) |
| ethos-u-vela | **4.4.1** |
| tosa-tools | git `tosa-reference-model` tag **v2025.07.1** (`bc8794f8…`) |
| flatbuffers (+ `flatc`) | **24.3.25** |
| numpy / ml_dtypes / lxml | 1.26.4 / 0.5.1 / 6.0.0 |

`model/requirements.txt` lists the direct dependencies. `model/requirements-lock.txt` is the
full `pip freeze` of the working environment.

---

## 3. Setup on a new Mac (about 30–45 min, mostly downloads)

### 3.1 Base tools

```bash
brew install git gh
brew install --cask segger-jlink visual-studio-code
gh auth login                       # HTTPS, browser device flow
mkdir -p ~/alif && cd ~/alif
gh repo clone Parth-Bansal30/e8-hackathon
```

### 3.2 Toolchain via VS Code

```bash
for e in arm.environment-manager arm.cmsis-csolution marus25.cortex-debug ms-vscode.cpptools-extension-pack; do code --install-extension $e; done
code ~/alif/e8-hackathon/firmware
```

Opening `firmware/` triggers the Environment Manager, which reads
`vcpkg-configuration.json` and downloads the toolchain into
`~/.vcpkg/artifacts/<hash>/`. Wait until the status bar shows the environment as active
(first run: a few minutes). The scripts find the `<hash>` folder by themselves. To use a
different install, set `CMSIS_TOOLS` to it.

### 3.3 Check the toolchain and install the packs

```bash
A=$(zsh -c 'print -l ~/.vcpkg/artifacts/*/tools.open.cmsis.pack.cmsis.toolbox/2.15.0(N:h:h) | head -1'); echo $A
export PATH="$A/tools.open.cmsis.pack.cmsis.toolbox/2.15.0/bin:$A/compilers.arm.arm.none.eabi.gcc/13.3.1/bin:$PATH"
cbuild --version; arm-none-eabi-gcc --version | head -1
cpackget init https://www.keil.com/pack/index.pidx
cpackget add -a https://github.com/ARM-software/CMSIS_6/releases/download/v6.0.0/ARM.CMSIS.6.0.0.pack
cpackget add -a https://github.com/alifsemi/alif_ensemble-cmsis-dfp/releases/download/v2.0.4/AlifSemiconductor.Ensemble.2.0.4.pack
for p in ARM::CMSIS-Compiler@2.1.0 Keil::MDK-Middleware@8.2.0 ARM::ethos-u-core-driver@1.26.5; do cpackget add -a "$p"; done
cpackget list
```

`build_flash.sh` also passes `--packs` to cbuild, which fetches any missing pack.

### 3.4 SETOOLS (flashing)

```bash
tar xf ~/Downloads/app-release-exec-macos-SE_FW_1.112.00_DEV.tar -C ~/alif   # -> ~/alif/app-release-exec-macos
cd ~/alif/app-release-exec-macos
export LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8                     # SETOOLS crashes without a UTF-8 locale
cp utils/global-cfg.db utils/global-cfg.db.bak
sed -i '' 's/"Revision": "A1"/"Revision": "A0"/' utils/global-cfg.db   # our board is chip revision A0
grep -E "Revision|Part#" utils/global-cfg.db
```

Expected: `"Revision": "A0"` and `"Part#": "E8 (AE822FA0E5597LS0) - 5.5 MRAM / 9.75 SRAM"`.
If the kit's chip revision is different, use that; `./maintenance` shows it.

Plug the board in through the **PRG USB** USB-C port (J3; J-Link and SETOOLS both use it). Then set its serial port in `isp_config_data.cfg`:

```bash
ls /dev/cu.usbmodem*        # pick the board's port
# first line of isp_config_data.cfg:  comport /dev/cu.usbmodemXXXXXXXXXXXX
```

If SETOOLS is somewhere else, `export ALIF_SETOOLS=/path/to/app-release-exec-macos`.

### 3.5 ExecuTorch runtime for the board

```bash
cd ~/alif/e8-hackathon && tools/fetch_et_bundle.sh     # prints "ExecuTorch libs ready: N"
```

### 3.6 Python export environment (only needed to retrain or re-export models)

tosa-tools is built from source, so cmake and ninja must be on PATH. Use the vcpkg ones.

```bash
cd ~/alif/e8-hackathon
A=$(zsh -c 'print -l ~/.vcpkg/artifacts/*/tools.open.cmsis.pack.cmsis.toolbox/2.15.0(N:h:h) | head -1')
export PATH="$(dirname $(find $A/tools.kitware.cmake -name cmake -type f | head -1)):$(dirname $(find $A/tools.ninja.build.ninja -name ninja -type f | head -1)):$PATH"
python3.12 -m venv et-venv && source et-venv/bin/activate
pip install --upgrade pip setuptools wheel
pip install "torch==2.9.0" "torchvision==0.24.0" "executorch==1.0.0" "ethos-u-vela==4.4.1" \
            "ml_dtypes==0.5.1" "flatbuffers==24.3.25" "lxml>=4.7.1,<6.0.1" pillow numpy==1.26.4
pip install "tosa-tools @ git+https://git.gitlab.arm.com/tosa/tosa-reference-model.git@v2025.07.1"
pip install "flatbuffers==24.3.25"      # tosa-tools downgrades it; the Arm backend needs 24.3.25
python -c "from executorch.backends.arm.ethosu import EthosUCompileSpec; print('arm backend OK')"
which flatc                              # must be et-venv/bin/flatc (ships with executorch)
```

Run the export scripts with the venv activated. `to_executorch()` fails with "No such file:
flatc" when `et-venv/bin` isn't on PATH.

---

## 4. Test sequence on the new machine (fast path, about 15 min)

Each step proves one more layer. Build + flash takes 10–40 s.

| # | Command | Expected result |
|---|---|---|
| 1 | `tools/build_flash.sh blinky` | `FLASHED blinky (debug)`; an LED blinks. Proves toolchain + SETOOLS + revision fix |
| 2 | `tools/build_flash.sh hello_rtt` then `tools/rtt_dump.sh firmware/out/hello_rtt/E8-HP/debug/hello_rtt.elf` | text from the board. Proves J-Link and RTT |
| 3 | `tools/build_flash.sh lcd_image` | test card on the LCD. Proves the display path |
| 4 | `tools/build_flash.sh mnist_executorch` + `rtt_dump.sh …/mnist_executorch.elf` | prediction for the built-in digit, ~304 µs `execute`. Proves ExecuTorch + NPU |
| 5 | `tools/build_flash.sh npu_display` | test digits cycling with prediction, confidence bars, latency |
| 6 | `tools/build_flash.sh npu_draw` | draw a digit with a finger; it's recognized live |
| 7 | `tools/build_flash.sh hw_test` + `rtt_dump.sh …/hw_test.elf` | dashboard; see below |
| 8 | `tools/build_flash.sh cam_test` | without a camera module: `sensor power + ID FAILED` (expected) |

**hw_test, by hand:**

- Press all 5 joystick directions.
- Turn the **blue R79 knob** (next to the joystick) end to end.
- Tilt the board.
- Clap or talk near it.
- Touch the screen.
- Check the two RGB LEDs cycle R, G, B, W.

Every row should turn OK. The NPU row is judged after one full pass of 10 test digits
(9/10 is normal: test #18 is always misread). The RTT log prints a status line every 2 s
(`[HW] t=… joy=… pot=… imu=… mic=… dB …`). With mic gain 0x800, a quiet room reads about
−53 dBFS.

Release builds: add `release`, e.g. `tools/build_flash.sh app release`.

**If something fails:** check [bringup-notes.md](bringup-notes.md) first. The common ones:

- **Flash refused:** wrong chip revision in `global-cfg.db`.
- **Board stuck after a replug:** SEROM recovery, error 0x30. Run `./maintenance` -> ROM -> MRAM Recovery.
- **Black screen:** the LVDS mux. Call `board_gpios_config()` after `board_pins_config()`.
- **NPU outputs identical for every input:** SRAM0/1 aren't powered.
- **cbuild "is a directory":** delete `firmware/tmp` and `firmware/out/<project>`.

---

## 5. Model workflows

### MNIST (done; artifacts committed)

```bash
cd model && source ../et-venv/bin/activate
python export_mnist.py --calib 500 --int8-io --out mnist_int8io.pte        # --train to retrain
../tools/pte2h.py mnist_int8io.pte ../firmware/npu_display/assets/mnist_model_data.h
cp mnist_int8io_io.h ../firmware/npu_display/assets/model_io.h
```

Expect float 98.00% / int8 98.05% on the host and a 105,696-byte .pte.

### Keyword spotting (trained and exported; board app built but not tested)

The trained checkpoint `kws_model.pth` and the exported `kws_ethos_u85.pte` / `_io.h` are
committed. Only retrain if you change the model or the features; `python export_kws.py`
alone rebuilds the .pte from the checkpoint in about a minute.

```bash
cd model && source ../et-venv/bin/activate
mkdir -p speech_commands && curl -fL https://storage.googleapis.com/download.tensorflow.org/data/speech_commands_v0.02.tar.gz | tar xz -C speech_commands   # 2.3 GB
python -u train_kws.py --epochs 25 | tee kws_train.log     # builds kws_cache/ (~1.5 GB, 23 s), then ~1 min/epoch on an M2 (MPS)
python export_kws.py                                       # kws_ethos_u85.pte + _io.h + ../firmware/app/kws_tables.h
python export_kws.py --untrained --out /tmp/x.pte          # only checks that the graph lowers
```

- **Model:** DS-CNN-S, 23,756 parameters, 12 classes. The classes are silence, unknown, and
  yes/no/up/down/left/right/on/off/stop/go.
- **Input:** 49x40 log-mel. The feature definition is in `model/kws_common.py`; the board's C
  version must match it exactly.
- **Results (25 epochs, 42–65 s each on an M2):**

  | Metric | Value |
  |---|---|
  | Validation accuracy (best epoch) | 95.27% |
  | Test accuracy, float, full test set (5,092 clips) | 94.89% |
  | Float (BatchNorm folded) vs int8, on 3,000 test clips | 94.50% vs **94.53%** |
  | NPU operators | 12 of 12 (none on the CPU) |
  | .pte size | 36,064 bytes |
  | Vela estimate | 0.36 ms per inference, 68 KiB SRAM |
  | int8 IO | input scale 0.02646, zp −30; output scale 0.05939, zp 1 |

  Main confusions: go -> no (16), down -> no (16), go/unknown (17). Silence is 509/509.

The dataset, cache and training log are git-ignored.

---

## 6. What was done, in order

1. **Board bring-up:**
   - The first board was faulty: its J-Link vanished from USB. It was replaced.
   - Cloned the VS Code template at the pinned commit and installed the toolchain and packs.
   - SETOOLS wouldn't flash until the chip revision was changed from A1 to A0.
   - `blinky` worked.
2. **Display:** `lcd_image` showed a black screen until the LVDS mux was configured. Panel
   variant 1, BGR byte order.
3. **ExecuTorch + Ethos-U85:** started from Arm's MNIST learning path and needed 4 fixes to
   get correct outputs: SRAM power, cache hooks, `LocalToGlobal` remap, int8 IO. Result:
   304 µs per inference, 98% accuracy.
4. **Demos:** `npu_display` (on-screen results, back buffer in SRAM1) and `npu_draw`
   (GT911 touch input + preprocessing like MNIST's).
5. **hw_test dashboard:** checks LEDs, joystick, pot (R79 on the E8, not R53, which is the E7
   board), IMU over I3C (needs flex I/O at 1.8 V), PDM mics, touch and NPU. Fixed the NPU row
   being stuck at FAIL.
6. **Repo:** moved everything into `~/alif/e8-hackathon` and made the tools work from any
   location (`$SolutionDir()$` link paths). Published as a public repo:
   https://github.com/Parth-Bansal30/e8-hackathon.
7. **Camera:**
   - Wrote `cam_test` (ARX3A0 over CSI-2, auto-exposure in software, live grey preview).
   - `RTE_ISP` is set to 0 so the project links without the ISP library.
   - The sensor never answered on I2C1 at 0x36. The reason: the kit has no camera module.
   - Connectors for later: J22 = Camera 1 (top, right edge), J16 = Camera 2 (underside).
   - Decision: drop vision and build the voice + motion project.
8. **Mic gain:**
   - Swept the PDM gain register live over J-Link. It's linear (4x = +12 dB).
   - Alif's demo value 0x0D leaves speech near −50 dBFS. Moved to 0x800/0xBB0.
9. **Keyword spotting (current):**
   - Downloaded Speech Commands v0.02.
   - Wrote the feature, model, training and export scripts, and checked that the model lowers
     entirely to the NPU.
   - Trained it: 94.9% test accuracy, and int8 loses nothing (94.53% vs 94.50% float).
   - Wrote the board-side capture and feature code (`firmware/app/kws.c`).
   - Turned `firmware/app` into the live keyword app. It builds and was flashed once, but
     work stopped before anyone looked at the screen or the log. **Untested.**
10. **Reproducibility:** pinned `ethos-u-core-driver@1.26.5`, made the scripts find the
    toolchain on their own, added `model/requirements-lock.txt`, and wrote this document.

---

## 7. Next steps (the plan for the final app)

1. ~~Train and export KWS~~ (done, see §5).
2. **Test the keyword app (built, flashed once, never checked).** What's in it:
   - `main.cpp`:
     - 16 kHz PDM capture.
     - One log-mel frame every 20 ms.
     - An inference every 100 ms on the latest 1 s of audio.
     - Scores averaged over 3 runs; a word is reported above 70%.
     - Screen: the word, per-class bars, mic level and timings.
   - `kws_testclip.h`: a boot self-test that runs the C features on a Speech Commands "yes"
     clip and compares them with Python.
   - `assets/kws_model_data.h` and `assets/kws_io.h`: the model, from `tools/pte2h.py`, and
     its int8 IO parameters.

   To test:
   - Run `tools/build_flash.sh app`, then `tools/rtt_dump.sh firmware/out/app/E8-HP/debug/app.elf`.
   - The first log line, `[KWS] self-test: …`, should show inputs within 1 LSB of Python and
     predict `'yes'`. If it doesn't, the C features don't match `model/kws_common.py`; fix
     that before trying live speech.
   - Then say the keywords about 30 cm from the board and watch for `[KWS] … 'word' NN%` lines.
     `dropped` in the status line should stay at 0.
   - Things that might need tuning:
     - **Mic channel:** only mic 4 (left of the pair) is used.
     - **Detection threshold:** 0.70.
     - **Speed:** the debug build redraws slowly; try `release`.
     - **The runner reloads the program on every call:** compare `runner` against `NPU` time.
4. Gesture model:
   - Record labeled BMI323 windows from the board over RTT (shake, flick left/right, tilt, idle).
   - Train a small 1D CNN and export it the same way.
5. Run several models on the NPU. The runner reloads the program on every call, so switching
   models only means passing a different buffer. Later, keep each model's method loaded to
   cut the reload time.
6. Fuse voice + motion + touch into one control-panel UI. Measure per-model latency and frame
   time in a release build.
7. Update the README's architecture, workload placement and performance sections. Then tag
   the demo commit.
