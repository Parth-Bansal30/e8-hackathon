# E8 Edge AI (working title)

Entry for the [Arm + ExecuTorch Edge AI hackathon](https://arm-executorch.devpost.com/),
running PyTorch models through ExecuTorch on the **Ethos-U85 NPU** of an
**Alif Ensemble E8 DevKit (DK-E8)**.

> **Final project: TBD.** The idea isn't decided yet. `firmware/app/` is the placeholder it
> will grow from. The placeholder already brings up the board, display and NPU, and runs a
> stand-in MNIST model so the whole pipeline stays tested.

## Status

What has been verified on the board so far:

| Area | Status | Demo |
|---|---|---|
| Build, flash (SETOOLS), RTT logging | working | `blinky`, `hello_rtt` |
| MIPI DSI display, 480x800 | working | `lcd_image` |
| ExecuTorch on Ethos-U85 (MNIST) | working: 304 µs per inference, int8 output matches the host | `mnist_executorch`, `npu_display` |
| Touch drawing -> NPU digit recognition | working | `npu_draw` |
| Joystick, BMI323 IMU, PDM mics, touch | pass the self-test | `hw_test` |
| RGB LEDs, user pot (R79) | self-test written, not confirmed yet | `hw_test` |
| ICM-42670P IMU, cameras | not tested yet | |

## Performance (MNIST CNN, int8)

| Metric | Value |
|---|---|
| Inference on Ethos-U85-256 (`method.execute`, M55-HP at 400 MHz) | **304 µs** (DWT cycle counter) |
| Accuracy, MNIST test set (host, int8 model) | 98.05% (float32: 98.00%) |
| On-board check, 10 test digits | 9/10 (test #18, an untidy 3, is misread) |
| Model size (.pte, Vela-compiled) | 105,696 bytes |
| `npu_draw` frame (touch, preprocessing, inference, redraw), debug -O0 build | ~135 ms, of which NPU 0.3 ms |

## Architecture and workload placement (current)

```
 PyTorch model --export_mnist.py--> int8 quantize (EthosUQuantizer, calibrated on 500 images)
   --> EthosUPartitioner + Vela 4.4.1 --> .pte --pte2h.py--> C array in MRAM
                                                                  |
 DK-E8:                                                           v
  sensors/touch --> Cortex-M55 HP @400 MHz --int8 tensor--> Ethos-U85-256 NPU
                    (drivers, preprocessing,                (whole CNN graph,
                     quantization, UI drawing)               delegated by ExecuTorch)
                           |
                           v
                    CDC200 + MIPI DSI --> 480x800 LCD
```

| Work | Runs on | Memory |
|---|---|---|
| Model weights and command stream (.pte) | read by the NPU | MRAM |
| CNN inference (all ops delegated) | Ethos-U85-256 | tensor arena in SRAM0 |
| Drivers, preprocessing, int8 (de)quantization, UI | Cortex-M55 HP | DTCM / SRAM1 back buffer |
| Display scan-out | CDC200 + MIPI DSI | frame buffer in SRAM0 |

To be updated once the final app is chosen. Unused so far: the M55-HE core and the two
Ethos-U55s.

## Repository layout

```
firmware/            CMSIS csolution, based on alifsemi/alif_vscode-template
  app/               final hackathon app (placeholder)
  hw_test/           peripheral self-test dashboard (LEDs, joystick, pot, IMU, mics, touch, NPU)
  npu_draw/          draw a digit on the touchscreen; the NPU recognizes it live
  npu_display/       cycles MNIST test digits through the NPU with on-screen results
  mnist_executorch/  minimal ExecuTorch runner + RTT output (shared executorch_runner.cpp)
  lcd_image/         display bring-up test card
  blinky/ hello/ hello_rtt/   template bring-up examples
model/               model export: export_mnist.py, trained checkpoint, exported .pte
tools/               build_flash.sh, rtt_dump.sh, fetch_et_bundle.sh, pte2h.py
docs/                bring-up notes (all the board gotchas and their fixes)
```

## Build and run

Prerequisites (macOS shown; Linux works too):

- Alif DK-E8 connected via the **PRG USB** port
- [CMSIS-Toolbox 2.15 + Arm GNU 13.3.1](https://github.com/alifsemi/alif_vscode-template) (installed by the VS Code template's environment manager)
- [Alif SETOOLS 1.112](https://alifsemi.com/support/kits/ensemble-e8devkit/), with the serial port set in `isp_config_data.cfg`. For A0 boards, see the [bring-up notes](docs/bringup-notes.md)
- SEGGER J-Link (on-board J-Link OB) for logs

```bash
tools/fetch_et_bundle.sh                # once: prebuilt ExecuTorch runtime for Cortex-M55 / Ethos-U
tools/build_flash.sh app                # build + flash to MRAM (any project; add `release` for an optimized build)
tools/rtt_dump.sh firmware/out/app/E8-HP/debug/app.elf   # read the board's log
```

`build_flash.sh` finds SETOOLS and the toolchain through `ALIF_SETOOLS` and `CMSIS_TOOLS`.
Set those if your install paths differ from the defaults in the script.

### Re-exporting the model

```bash
python3 -m venv .venv && source .venv/bin/activate
pip install -r model/requirements.txt && pip install flatbuffers==24.3.25
cd model
python export_mnist.py --calib 500 --int8-io --out mnist_int8io.pte          # add --train to retrain
../tools/pte2h.py mnist_int8io.pte ../firmware/npu_display/assets/mnist_model_data.h
cp mnist_int8io_io.h ../firmware/npu_display/assets/model_io.h
```

## Credits and licenses

- `firmware/` builds on [alifsemi/alif_vscode-template](https://github.com/alifsemi/alif_vscode-template) (Alif license, `firmware/License.txt`) and SEGGER RTT (`firmware/libs/SEGGER_RTT_V796h/LICENSE.md`).
- The ExecuTorch runner and MNIST model are adapted from Arm's [alif-ethos-u85-npu-mnist](https://github.com/arm-education/alif-ethos-u85-npu-mnist) learning path. `tools/fetch_et_bundle.sh` downloads its prebuilt runtime.
