# DK-E8 bring-up notes

Problems hit while getting ExecuTorch, the display and the peripherals working on the
Alif Ensemble E8 DevKit (DK-E8), and the fix for each. Everything in `firmware/` already
includes these fixes.

## Board and tools

| Symptom | Cause | Fix |
|---|---|---|
| `app-write-mram` refuses to flash | Board is chip revision **A0**; SETOOLS 1.112 defaults to A1 | Set `"Revision": "A0"` in SETOOLS `utils/global-cfg.db` (check with `maintenance -opt getrevision`) |
| SETOOLS `-h` crashes | Non-UTF-8 locale | `export LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8` |
| After a replug the board sits in SEROM recovery (error 0x30, invalid TOC) | Interrupted or invalid MRAM image | `./maintenance` -> ROM -> MRAM Recovery, then flash again |
| `JLinkRTTLogger` can't attach | | `tools/rtt_dump.sh` reads the RTT buffer straight from memory |
| RTT buffer size setting ignored | The Arm tutorial's config uses the wrong macro name | Define `BUFFER_SIZE_UP`, not `SEGGER_RTT_BUFFER_SIZE_UP` |
| `CoreDebug` undeclared | Removed in CMSIS 6 | Use `DCB` |

## Display (MIPI DSI, ILI9806E 480x800)

- The DK-E8 routes the panel through a **MIPI/LVDS mux**. Call `board_gpios_config()` after
  `board_pins_config()` and before starting the CDC200, or the backlight comes on but the
  screen stays black.
- Panel variant 1 (the pack default) works. Pixel format RGB888, bytes in **B, G, R** order.
- The frame buffer lives in SRAM0, which is not powered by default (see below).
- Drawing straight into the scanned buffer tears. Draw into a back buffer in SRAM1, then
  `memcpy` and `SCB_CleanDCache()`.

## ExecuTorch on the Ethos-U85

Arm's MNIST tutorial runs but gives the same output for every input. Four fixes were needed:

1. **Power SRAM0 and SRAM1.** They are off at boot and the tutorial's power-up is a no-op, so
   CPU writes to the tensor arena are lost and the NPU reads zeros. Fix:
   `SERVICES_get_run_cfg`, then set `memory_blocks |= MRAM_MASK | SRAM0_MASK | SRAM1_MASK`,
   then `SERVICES_set_run_cfg`.
2. **Cache maintenance.** The Ethos-U driver's weak `ethosu_flush_dcache` and
   `ethosu_invalidate_dcache` are empty. Override them with `SCB_CleanDCache()` and
   `SCB_CleanInvalidateDCache()`.
3. **Address remap.** The NPU can't see the M55's DTCM through local addresses. Override
   `ethosu_address_remap()` to return `LocalToGlobal(address)`.
4. **int8 model IO.** The prebuilt bundle's CPU quantize and dequantize ops misbehave. Export
   with int8 inputs and outputs (`export_mnist.py --int8-io`) and do the (de)quantization in
   the app using the generated `*_io.h` scale and zero-point.

Fixes 2 and 3 are in `firmware/mnist_executorch/executorch_runner.cpp`. Fix 1 is in each
app's `platform_init()`.

## Peripherals

| Peripheral | Where | Notes |
|---|---|---|
| RGB LED0 / LED1 | GPIO12_3, GPIO7_4, GPIO12_0 / GPIO6_2, 6_4, 6_6 | LED0 green is on flex I/O |
| Joystick | LPGPIO15 pins 0..4 | Active low; the app reads the idle level at start |
| User pot | Blue knob **R79** beside the joystick, into ADC121 input 1 (0..1.8 V) | Single-shot conversion |
| IMUs | BMI323 and ICM-42670P on I3C, flex I/O P7_6/P7_7 | Needs `vdd_ioflex_3V3 = IOFLEX_LEVEL_1V8`. Only one IMU driver can be linked (duplicate symbols). RTE_Device.h and board_defs disagree on the INT pins, so poll instead of using interrupts |
| PDM mics | Channels 4/5 (PDM_D2_B, PDM_C2_A) | Clock `CLKEN_HFOSCx2`; settings copied from Alif's `demo_pdm.c` |
| Touch | GT911 on I2C1, INT GPIO9_4, RST GPIO6_5 | Raw coordinates equal screen pixels. `GetState` only reports on a new interrupt, so treat 150 ms with no reports as lift-off |
| Camera | ARX3A0 / AR0144 on CSI-2, I2C1, power GPIO7_5, reset GPIO9_1 | Not brought up yet |

- `Driver_ADC.h` and `Driver_PDM.h` don't compile as C++ (`typedef const struct`), so
  `hw_test` is plain C.
- The ADC needs `sys_ctrl_analog.c`, which no pack component pulls in. A copy is in
  `firmware/hw_test/vendor/`.
- If cbuild fails with "read ...: is a directory", the `firmware/tmp/` cache is corrupt.
  Delete `firmware/tmp` and `firmware/out/<project>`.
